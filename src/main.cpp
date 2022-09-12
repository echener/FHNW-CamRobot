#include <Arduino.h>

#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

#include "esp_camera.h"

// The servo controller PCA9685 librarie is loaded and the servo controller itsef
// is connected to the I2C bus.
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

// Select camera model
#define CAMERA_MODEL_AI_THINKER

#include "camera_pins.h"

// define the variables for the I2C pins with pin numbers
#define LED_PIN 4
#define I2C_SDA 14
#define I2C_SCL 15
#define CHANNEL_L_F 2
#define CHANNEL_L_B 3
#define CHANNEL_R_F 1
#define CHANNEL_R_B 0
#define SERVO_CHANNEL 4
#define BUZZER_CHANNEL 5
#define PWM_FREQ 50

#define SPEED_MIN 50
#define SPEED_SLOW 70
#define SPEED_MAX 100
#define SERVOMIN 200  // This is the 'minimum' pulse length count (out of 4096)
#define SERVOMAX 400  // This is the 'maximum' pulse length count (out of 4096)

// Update those settings with the login information of you wifi
#define ssid "frc"
#define password "pwd01"

// Those variables are used to control the servo controller
// and to update the OLED display with some status
// and some kind of debug information.
// I knew it is not a really nice way how I programmed that...

extern String command = "stop";
extern int speed = 85;
extern boolean switch_led = false;
extern boolean buzzer = false;
extern int servo_pos = 0;

boolean led_state = false;
int current_servo_pos = 20;

// That variable is used to store the IP-address inside
// for the HTML code to link the live video stream
extern String Camerafeed = "";

void startCameraServer();

void blink() {
    digitalWrite(4, HIGH);
    delay(5);
    digitalWrite(4, LOW);
    delay(195);
}

void buzzOn() {
    pwm.setPWM(BUZZER_CHANNEL, 0, 4095);
}

void buzzOff() {
    pwm.setPWM(BUZZER_CHANNEL, 0, 0);
}

void set_servo(uint8_t percent) {
    if (percent > 100) {
        percent = 100;
    }
    int pulse = SERVOMIN + 2 * percent;
    pwm.setPWM(SERVO_CHANNEL, 0, pulse);
}

void set_pwm_channel(int channel, int power_percent) {
    if (power_percent > 100) {
        power_percent = 100;
    }
    int duty_on = int(40.95 * power_percent);
    pwm.setPWM(channel, 0, duty_on);
}

void set_left_wheel(int speed) {
    if (speed > 0) {
        set_pwm_channel(CHANNEL_L_B, 0);
        set_pwm_channel(CHANNEL_L_F, speed);
    } else {
        if (speed < 0) {
            set_pwm_channel(CHANNEL_L_F, 0);
            set_pwm_channel(CHANNEL_L_B, -1 * speed);
        } else {
            set_pwm_channel(CHANNEL_L_B, 0);
            set_pwm_channel(CHANNEL_L_F, 0);
        }
    }
}

void set_right_wheel(int speed) {
    if (speed > 0) {
        set_pwm_channel(CHANNEL_R_B, 0);
        set_pwm_channel(CHANNEL_R_F, speed);
    } else {
        if (speed < 0) {
            set_pwm_channel(CHANNEL_R_F, 0);
            set_pwm_channel(CHANNEL_R_B, -1 * speed);
        } else {
            set_pwm_channel(CHANNEL_R_F, 0);
            set_pwm_channel(CHANNEL_R_B, 0);
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(200);

    Serial.println("Test");

    pinMode(LED_PIN, OUTPUT);

    blink();
    //Serial.setDebugOutput(true);
    Serial.println();

    // Adafruit library servo controller setup
    Wire.begin(I2C_SDA, I2C_SCL);
    pwm.begin();
    pwm.setOscillatorFrequency(27000000);
    pwm.setPWMFreq(PWM_FREQ);
    Wire.setClock(400000);
    delay(100);
    set_left_wheel(0);
    set_right_wheel(0);

    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    // init with high specs to pre-allocate larger buffers
    if (psramFound()) {
        Serial.println("psram found.");
        // config.frame_size = FRAMESIZE_UXGA;
        config.frame_size = FRAMESIZE_VGA;
        config.jpeg_quality = 10;
        // config.jpeg_quality = 4;
        config.fb_count = 2;
        blink();

    } else {
        Serial.println("no psram found.");
        blink();
        blink();
        blink();

        config.frame_size = FRAMESIZE_SVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
    }
    delay(200);
    // camera init
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x", err);
        blink();
        return;
    }

    // AP mode
    WiFi.softAP("frc", NULL);
    Camerafeed = WiFi.softAPIP().toString().c_str();
    /*
    // or station mode
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("");

    Serial.println("WiFi connected");

    Camerafeed = WiFi.localIP().toString().c_str();

    */

    startCameraServer();

    blink();
    set_servo(servo_pos);
}

void loop() {
    Serial.println("hallo");
    if (command == "stop") {
        set_left_wheel(0);
        set_right_wheel(0);
    }
    if (command == "left") {
        set_left_wheel(0);
        set_right_wheel(SPEED_MIN);
    }
    if (command == "right") {
        set_left_wheel(SPEED_MIN);
        set_right_wheel(0);
    }
    if (command == "forward") {
        set_left_wheel(SPEED_MIN);
        set_right_wheel(SPEED_MIN);
    }
    if (command == "back") {
        set_left_wheel(-SPEED_MIN);
        set_right_wheel(-SPEED_MIN);
    }
    if (switch_led) {
        if (led_state) {
            digitalWrite(LED_PIN, LOW);

            led_state = false;
        } else {
            digitalWrite(LED_PIN, HIGH);

            led_state = true;
        }

        switch_led = false;
    }
    if (servo_pos != current_servo_pos) {
        set_servo(servo_pos);
        current_servo_pos = servo_pos;
    }
    if (buzzer) {
        buzzOn();
        delay(100);
        buzzOff();
        buzzer = false;
    }
}
