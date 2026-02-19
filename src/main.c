#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>


LOG_MODULE_REGISTER(my_state_machine, LOG_LEVEL_INF);


//durumlarımı tanımlamak için typedef ile enum 
typedef enum {
    STATE_INIT, //ilk ayarlamalar için kullanılacak ve aslında bir state olmasına gerek yok. mainInit şeklinde de yapılabilir.
    STATE_IDLE, //boş durarken veya bu örnekte kart beklerken 
    STATE_ACTIVE, //işlem algılandığında  bu örnekte turnike döndürme işlemi yaparken.
    STATE_ERROR // olası bir hata durumunda bu örnekte kart hatalı geldiğinde vs.

} app_state_tasks;

//state machine için bir context (durum) yapısı, yani bunu kullanarak statemachine yapacağız tipi bu.
struct sm_context {
    app_state_tasks current_state; //mevcut durumu tutacak
    struct k_work_delayable sm_work; // gecikmeli veya anlık çalışabilen work itemini tutacak
    uint8_t process_counter; //işlemlerin sayısını takip etmek için kullanılacak.
}


struct sm_context my_sm; //kendi state machine değişkenimiz.


//fonksiyon prototipleri 
void state_init_run(struct sm_context *ctx); //hepsi toplu haldeki contexti alıyor, birleştirilmiş kwork değişkeni ile.
void state_idle_run(struct sm_context *ctx);
void state_active_run(struct sm_context *ctx);
void state_error_run(struct sm_context *ctx);


//ana work handler, tüm kontrolleri yapıp durumları dağıtacak olan şekli ile

void sm_work_handler(struct k_work *work){
    // k_work objesinden kendi context yapımıza ulaşıyoruz (Zephyr'in makrosudur)
    //burada ctx aslında hem durum hem sayaç gibi değişkenleri tutan bir struct, daha okunabilir ve toplu olması için.
    //fonksiyonlara sadece ctx göndereceğiz o oradan çekecek kendi mevzusuna ait olanları.
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct sm_context *ctx = CONTAINER_OF(dwork, struct sm_context, sm_work); //comtainerof geri pointer döndürüyor, içinde itemler olan structları verince.


    //ctx yani context yani mevcut duruma göre fonksiyonları çağırıyor.
    switch (ctx->current_state){
        case STATE_INIT:
            state_init_run(ctx);
            break;
        case STATE_IDLE:
            state_idle_run(ctx);
            break;
        case STATE_ACTIVE:
            state_active_run(ctx);
            break;     
        case STATE_ERROR:
            state_error_run(ctx);
            break;
        default:
            LOG_ERR("Unknown state!");
            break;
        }
}


void state_init_run(struct sm_context *ctx){
//bu fonksiyon ilk ayarları yaptıktan sonra direkt state'ini güncelleyip
//aktif state'i idle'a çekecek. Yani cihaz kurulumdan sonra kart bekleyecek.
    LOG_INF("Current State is INIT, Setting up the system.");
    ctx->process_counter = 0; //işlem sayacını sıfırla

    //BURADA BAŞKA İŞLEMLER DE YAPABİLİR.

    LOG_INF("INIT is done! Current is setting up to IDLE now.");
    ctx->current_state = STATE_IDLE; //durumu idle yap.

    // İşlemin 'hemen' devam etmesi için system workqueue'ya tekrar ekle
    k_work_submit(&ctx->sm_work.work);

};


void state_idle_run(struct sm_context *ctx){
    LOG_INF("Current State is IDLE, Waiting for events.");
    
    //BURADA EVENT OLUYOR MU DİYE BAKACAK ek bir flag ile kontrol sağlanacak mesela.
    
    // --- EXIT / TRANSITION KISMI ---
    LOG_INF("DEMO: [IDLE] -> [ACTIVE] gecisi icin 2 saniye sayiliyor.");
    ctx->current_state = STATE_ACTIVE;
    
    // Zephyr workqueue'nun gücü: while ile beklemek yerine 
    // work item'ı 2 saniye sonraya "zamanlıyoruz". O sırada işlemci uyuyabilir.
    k_work_schedule(&ctx->sm_work, K_SECONDS(2));
};


void state_active_run(struct sm_context *ctx){
    LOG_INF("Current State is ACTIVE, Processing events. process counter: %d ", ctx->process_counter);
    
     //TODO: BURADA İŞLEMLER YAPILACAK, ÖRNEĞİN KART OKUMA, TURNİKE DÖNDÜRME VS.
    
    ctx->process_counter++; //işlem sayacını artır
    
    //TODO: işte içinde saysın, eğer 5 saniye işlem olmazsa kapansın ve idle'a dönsün vs.
    if (ctx->process_counter > 4){
        LOG_INF("DEMO: [ACTIVE] -> [IDLE] because it processed 5 times. ");
        ctx->current_state = STATE_IDLE;
        k_work_schedule(&ctx->sm_work, K_MSEC(500)); //yarım saniye sonra idle'a geçiş yapalım.
    }  
    else {
        // Sayı doldu, state machine görevini tamamladı.
        LOG_INF("[ACTIVE] Islem tamamlandi. State Machine duruyor.");
        // KENDİNİ TEKRAR ÇAĞIRMIYOR (Submit yok). Kuyruk burada sonlanır.
    }
};
void state_error_run(struct sm_context *ctx){

    LOG_INF("Current State is ERROR, Your Card is not valid!\n");

    //TODO ERROR FLAG ONA GÖRE KONTROL VE İŞLEM OLACAK.

    LOG_INF("DEMO: [ERROR] -> [IDLE] \n");
    ctx->current_state = STATE_IDLE; //hata durumundan sonra tekrar idle'a dönelim.
    k_work_schedule(&ctx->sm_work, K_MSEC(1500)); //1.5 saniye sonra idle'a geçiş yapalım. listeye de 1.5 saniye sonra ekle. o sırada diğer işlemleri yapabilirsin.
};


int main(void){

    my_sm.current_state = STATE_INIT;
    //zaten initten sonra direkt idle'a geçecek.

    // Work item'ı ilgili handler (yönlendirici) ile ilişkilendir, ilk kısımda çalışıyor ve köprü kuruyor.
    k_work_init_delayable(&my_sm.sm_work, sm_work_handler);

    // State machine'i ateşle! (İlk çalışmayı kuyruğa at, direkt listenin en sonuna ekler annda çalıştırır gibi)
    k_work_submit(&my_sm.sm_work.work); 

    // State machine arka planda system workqueue üzerinde kendi kendine dönecektir.
    return 0;

}