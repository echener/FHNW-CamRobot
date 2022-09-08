// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Ingmar Stapel: 20201002 did some changes.
// Homepage: https://custom-build-robots.com
// Many thanks go to: https://iotdesignpro.com/projects/esp32-cam-surveillance-robot-car
// for doing that greate job to enhance the code to control a robot car.

#include "Arduino.h"
#include "camera_index.h"
#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "img_converters.h"

extern String Camerafeed;

extern String command;
extern boolean switch_led;
extern boolean buzzer;
extern int servo_pos;

typedef struct {
    size_t size;   // number of values used for filtering
    size_t index;  // current value index
    size_t count;  // value count
    int sum;
    int* values;  // array to be filled with values
} ra_filter_t;

typedef struct {
    httpd_req_t* req;
    size_t len;
} jpg_chunking_t;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static ra_filter_t ra_filter;
httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

static ra_filter_t* ra_filter_init(ra_filter_t* filter, size_t sample_size) {
    memset(filter, 0, sizeof(ra_filter_t));

    filter->values = (int*)malloc(sample_size * sizeof(int));
    if (!filter->values) {
        return NULL;
    }
    memset(filter->values, 0, sample_size * sizeof(int));

    filter->size = sample_size;
    return filter;
}

static int ra_filter_run(ra_filter_t* filter, int value) {
    if (!filter->values) {
        return value;
    }
    filter->sum -= filter->values[filter->index];
    filter->values[filter->index] = value;
    filter->sum += filter->values[filter->index];
    filter->index++;
    filter->index = filter->index % filter->size;
    if (filter->count < filter->size) {
        filter->count++;
    }
    return filter->sum / filter->count;
}

static size_t jpg_encode_stream(void* arg, size_t index, const void* data, size_t len) {
    jpg_chunking_t* j = (jpg_chunking_t*)arg;
    if (!index) {
        j->len = 0;
    }
    if (httpd_resp_send_chunk(j->req, (const char*)data, len) != ESP_OK) {
        return 0;
    }
    j->len += len;
    return len;
}

static esp_err_t stream_handler(httpd_req_t* req) {
    camera_fb_t* fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t* _jpg_buf = NULL;
    char* part_buf[64];

    static int64_t last_frame = 0;
    if (!last_frame) {
        last_frame = esp_timer_get_time();
    }

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK) {
        return res;
    }

    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            Serial.printf("Camera capture failed");
            res = ESP_FAIL;
        } else {
            if (fb->format != PIXFORMAT_JPEG) {
                bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
                esp_camera_fb_return(fb);
                fb = NULL;
                if (!jpeg_converted) {
                    Serial.printf("JPEG compression failed");
                    res = ESP_FAIL;
                }
            } else {
                _jpg_buf_len = fb->len;
                _jpg_buf = fb->buf;
            }
        }
        if (res == ESP_OK) {
            size_t hlen = snprintf((char*)part_buf, 64, _STREAM_PART, _jpg_buf_len);
            res = httpd_resp_send_chunk(req, (const char*)part_buf, hlen);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char*)_jpg_buf, _jpg_buf_len);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }
        if (fb) {
            esp_camera_fb_return(fb);
            fb = NULL;
            _jpg_buf = NULL;
        } else if (_jpg_buf) {
            free(_jpg_buf);
            _jpg_buf = NULL;
        }
        if (res != ESP_OK) {
            break;
        }
        int64_t fr_end = esp_timer_get_time();

        int64_t frame_time = fr_end - last_frame;
        last_frame = fr_end;
        frame_time /= 1000;
        uint32_t avg_frame_time = ra_filter_run(&ra_filter, frame_time);
        Serial.printf("MJPG: %uB %ums (%.1ffps), AVG: %ums (%.1ffps)", (uint32_t)(_jpg_buf_len),
                      (uint32_t)frame_time, 1000.0 / (uint32_t)frame_time,
                      avg_frame_time, 1000.0 / avg_frame_time);
    }

    last_frame = 0;
    return res;
}

static esp_err_t index_handler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    String page = "";
    page += "<TITLE>CAM ROBOT CONTROL</TITLE>";
    page += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=0\">\n";
    page +=
        "<script>\
        var xhttp = new XMLHttpRequest();\
        function getsend(arg) { xhttp.open('GET', arg + '?' + new Date().getTime(), true); xhttp.send() }\
        function updateSpeedSlider(slideAmount) {\
            var sliderDiv = document.getElementById('speedSliderAmount'); getsend('speed/' + slideAmount); sliderDiv.innerHTML = 'Speed: ' + slideAmount;}\
        function updateServoSlider(slideAmount) {\
            var sliderDiv = document.getElementById('servoSliderAmount'); getsend('servo/' + slideAmount); sliderDiv.innerHTML = 'Servo: ' + slideAmount;}\
    </script>";

    page += "<p align=center><IMG SRC='http://" + Camerafeed + ":81/stream' style='width:320px; transform:rotate(0deg);'></p><br/>";

    page += "<p align=center>";
    page += "<button style=width:90px;height:30px onmousedown=getsend('right') ontouchstart=getsend('right') onmouseup=getsend('stop') >^</button>&nbsp;";
    page += "<button style=width:90px;height:30px onmousedown=getsend('left') ontouchstart=getsend('left') onmouseup=getsend('stop') >^</button>";
    page += "</p>";

    page += "<br>";
    page += "<p align=center>";

    page += "<button style=width:90px;height:30px onclick=getsend('led')>LED</button>&nbsp;";
    page += "<button style=width:90px;height:30px onmousedown=getsend('buzzer')  ontouchstart=getsend('buzzer') >&#128227;</button>";

    page += "</p>";
    page +=
        "<div align=center>\
        <input style = width:200px; height:30px type = 'range' min = '0' max = '100' step = '100' value = '" +
        String(servo_pos) +
        "' class = 'slider' id = 'servo' onchange = 'updateServoSlider(this.value)' >\
        <p id = 'servoSliderAmount'>Servo: " +
        String(servo_pos) +
        "</p>\
        </div><br/>";

    return httpd_resp_send(req, &page[0], strlen(&page[0]));
}

static esp_err_t left_handler(httpd_req_t* req) {
    command = "left";

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, "OK", 2);
}

static esp_err_t right_handler(httpd_req_t* req) {
    command = "right";

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, "OK", 2);
}

static esp_err_t stop_handler(httpd_req_t* req) {
    command = "stop";

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, "OK", 2);
}

static esp_err_t led_handler(httpd_req_t* req) {
    switch_led = true;
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, "OK", 2);
}

static esp_err_t buzzer_handler(httpd_req_t* req) {
    buzzer = true;
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, "OK", 2);
}

static esp_err_t servo_handler(httpd_req_t* req) {
    Serial.println(req->uri);
    // /servo/100?1660209262789
    // /servo/<slider-value>?1660209262789
    String servo_str = String(req->uri);
    servo_str = servo_str.substring(7, servo_str.indexOf("?"));
    Serial.print("servo = ");
    servo_pos = servo_str.toInt();
    Serial.println(servo_pos);
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, "OK", 2);
}

void startCameraServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 15;
    config.max_resp_headers = 15;
    config.uri_match_fn = httpd_uri_match_wildcard;
    
    httpd_uri_t left_uri = {
        .uri = "/left",
        .method = HTTP_GET,
        .handler = left_handler,
        .user_ctx = NULL};

    httpd_uri_t right_uri = {
        .uri = "/right",
        .method = HTTP_GET,
        .handler = right_handler,
        .user_ctx = NULL};

    httpd_uri_t stop_uri = {
        .uri = "/stop",
        .method = HTTP_GET,
        .handler = stop_handler,
        .user_ctx = NULL};

    httpd_uri_t led_uri = {
        .uri = "/led",
        .method = HTTP_GET,
        .handler = led_handler,
        .user_ctx = NULL};

    httpd_uri_t buzzer_uri = {
        .uri = "/buzzer",
        .method = HTTP_GET,
        .handler = buzzer_handler,
        .user_ctx = NULL};

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL};

    httpd_uri_t servo_uri = {
        .uri = "/servo/*",
        .method = HTTP_GET,
        .handler = servo_handler,
        .user_ctx = NULL};

    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL};

    ra_filter_init(&ra_filter, 20);
    Serial.printf("Starting web server on port: '%d'", config.server_port);
    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &left_uri);
        httpd_register_uri_handler(camera_httpd, &right_uri);
        httpd_register_uri_handler(camera_httpd, &stop_uri);
        httpd_register_uri_handler(camera_httpd, &led_uri);
        httpd_register_uri_handler(camera_httpd, &buzzer_uri);
        httpd_register_uri_handler(camera_httpd, &servo_uri);

        // httpd_register_uri_handler(stream_httpd, &stream_uri);
    }

    config.server_port += 1;
    config.ctrl_port += 1;
    Serial.printf("Starting stream server on port: '%d'", config.server_port);
    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }
}
