#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>


LOG_MODULE_REGISTER(my_state_machine, LOG_LEVEL_INF);

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(DT_ALIAS(button0), gpios, {0});
static struct gpio_dt_spec idle_led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {0});
static struct gpio_dt_spec gate_led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led1), gpios, {0});
static struct gpio_dt_spec error_led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led2), gpios, {0});
static struct gpio_callback button_cb_data;

bool buttonPressedFlag = false;

void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    buttonPressedFlag = true;
}


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
};


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
//bu fonksiyon ilk kurulum ayarlarını yaptıktan sonra direkt state'ini güncelleyip
//aktif state'i idle'a çekecek. Yani cihaz kurulumdan sonra kart bekleyecek.

    LOG_INF("Current State is INIT, Setting up the system.");
    ctx->process_counter = 0; //işlem sayacını sıfırla

    int ret;

	if (!gpio_is_ready_dt(&button)) {
		printk("Error: button device %s is not ready\n",
		       button.port->name);
		return 0;
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret != 0) {
		printk("Error %d: failed to configure %s pin %d\n",
		       ret, button.port->name, button.pin);
		return 0;
	}

	ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		printk("Error %d: failed to configure interrupt on %s pin %d\n",
			ret, button.port->name, button.pin);
		return 0;
	}

	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb_data);

    if (gate_led.port) {
		ret = gpio_pin_configure_dt(&gate_led, GPIO_OUTPUT);
		if (ret != 0) {
			printk("Error %d: failed to configure LED device %s pin %d\n",
			       ret, gate_led.port->name, gate_led.pin);
		} else {
			printk("Set up LED at %s pin %d\n", gate_led.port->name, gate_led.pin);
		}
	}

    if (idle_led.port) {
		ret = gpio_pin_configure_dt(&idle_led, GPIO_OUTPUT);
		if (ret != 0) {
			printk("Error %d: failed to configure LED device %s pin %d\n",
			       ret, idle_led.port->name, idle_led.pin);
		} else {
			printk("Set up LED at %s pin %d\n", idle_led.port->name, idle_led.pin);
		}
	}

    if (error_led.port) {
		ret = gpio_pin_configure_dt(&error_led, GPIO_OUTPUT);
		if (ret != 0) {
			printk("Error %d: failed to configure LED device %s pin %d\n",
			       ret, error_led.port->name, error_led.pin);
		} else {
			printk("Set up LED at %s pin %d\n", error_led.port->name, error_led.pin);
		}
	}

    LOG_INF("INIT is done! Current STATE is setting up to IDLE now, waiting for CARD input. (Button 0) ");
    ctx->current_state = STATE_IDLE; //durumu idle yap.

    // İşlemin 'hemen' devam etmesi için system workqueue'ya tekrar ekle
    k_work_submit(&ctx->sm_work.work);

};


void state_idle_run(struct sm_context *ctx){
    LOG_INF("Current State is IDLE, Waiting for events.");
    gpio_pin_set_dt(&error_led, 0);//eğer errordan geliyorsa idle'a geri dönünce error kapansın diye düzenlenecek.
    
        if (buttonPressedFlag==true){
            // --- EXIT / TRANSITION KISMI ---
            LOG_INF("DEMO: [IDLE] -> [ACTIVE] because Button 0 Pressed ");
            // Zephyr workqueue'nun gücü: while ile beklemek yerine work item'ı 1 saniye sonraya "zamanlayabiliriz". O sırada işlemci uyuyabilir.
            // ama burada direkt aktif state'e geçsin istiyorum.
            ctx->current_state = STATE_ACTIVE;
            k_work_submit(&ctx->sm_work.work);
        }
        else{
            //sleep 
        }
}


void state_active_run(struct sm_context *ctx){
    LOG_INF("Current State is ACTIVE, Processing events. process counter: %d ", ctx->process_counter);
    // Her bir yarım saniyede bir bu fonksiyon çalışacak. 
    // 5 tam aç/kapa = 10 yarım adım eder. Sayacı 10'a kadar saydıracağız.
    if (ctx->process_counter < 10) {
        
        gpio_pin_toggle_dt(&gate_led);
        ctx->process_counter++; // Sayacı 1 artır
        
        // SİSTEMİ UYUTMAMAK İÇİN! Kendini 500ms sonra tekrar çalışması için kuyruğa ekle.
        // Bu sayede o 500ms boyunca işlemci başka işleri halledebilir.
        k_work_schedule(&ctx->sm_work, K_MSEC(500)); 
        
    } else {
        LOG_INF("DEMO: [ACTIVE] -> [IDLE] because it processed for 5 seconds.");
        
        gpio_pin_set_dt(&gate_led, 0);// gate led kapansın
        
        // IDLE'a geçiyoruz
        ctx->current_state = STATE_IDLE;
        
        // Bir sonraki sefer (başka biri turnikeden geçerse) sayacın sıfırdan 
        // başlaması için sayacı mutlaka sıfırlıyoruz.
        ctx->process_counter = 0; 
        
        // Beklemeden direkt IDLE fonksiyonuna geçiş yapması için hemen tetikle.
        k_work_submit(&ctx->sm_work.work); 
    }
};

void state_error_run(struct sm_context *ctx){
   
    LOG_INF("Current State is ERROR, Your Card is not valid!\n");

    //TODO ERROR FLAG ONA GÖRE KONTROL VE İŞLEM OLACAK.
    gpio_pin_set_dt(&error_led, 1);

    LOG_INF("DEMO: [ERROR] -> [IDLE]");
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