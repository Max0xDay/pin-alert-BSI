/*
 *   Author: Max Day
 *   Description: BSI Pin voltage drop system based on api
 *   System will read in voltages using a single channel opto isolator voltage detection sensor measuring high or low voltages to test if the system is on or not.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <curl/curl.h>
#include <time.h>
#include <wiringPi.h>
#include <signal.h>

/*
* https://github.com/WiringPi/WiringPi
* libcurl4-openssl-dev
to run : gcc Main.c -o pinalertv1 -lwiringPi -lcurl && ./pinalertv1

+--------------+----------------+----------------------+
| Physical Pin | WiringPi Pin   | Purpose              |
+--------------+----------------+----------------------+
|      3       |       8        |                      |
|      5       |       9        |                      |
|      7       |       7        |                      |
|     11       |       0        |                      |
|     13       |       2        |                      |
|     15       |       3        |                      |
|     19       |      12        |                      |
|     21       |      13        |                      |
|     23       |      14        |                      |
|     29       |      21        |                      |
+--------------+----------------+----------------------+ */

// Configuration
#define API_ENDPOINT "http://192.168.1.91:4000" // API's allow us to have the client ip be dynamic only the servers ip needs to be defined
#define DEVICE_SITE_NAME "testbuilding" // This should be set to what the site is called for easier identification of the device 
int CHECK_INTERVAL = 10;  
int STATUS_INTERVAL = 2;  
#define COMMAND_CHECK_INTERVAL 1
//Limiting number of pins for now 
const int PIN[] = {0,2,3,7,8};

struct ResponseData {
    char *data;
    size_t size;
};

size_t write_callback(void * contents, size_t size, size_t nmemb, void * userp) {
  return size * nmemb;
}

// site_name, timestamp, status, pin0, pin2, pin3, pin7, pin8, pin9, pin12, pin13, pin14, pin21
int send_data_request(float PIN[]) {
    CURL *curl;
    CURLcode res;
    int http_code = 0;
    time_t now;
    char timestamp[30];
    time(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S.000Z", localtime(&now));
    char payload[512];
    snprintf(payload, sizeof(payload),
        "{\"sitename\":\"%s\",\"timestamp\":\"%s\",\"pin0\":%.2f,\"pin2\":%.2f,\"pin3\":%.2f,\"pin7\":%.2f,\"pin8\":%.2f}",
        DEVICE_SITE_NAME,timestamp, PIN[0], PIN[1], PIN[2], PIN[3], PIN[4]);

    curl = curl_easy_init();
    if (curl) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_URL, ({
            char url[256];
            snprintf(url, sizeof(url), "%s/sensors", API_ENDPOINT);
            url;
        }));
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);

        res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    return http_code;
}
//TODO probs remove this as status update needs to be replaced by health pin
/**
 * Sends a status update to the server.
 *
 * This function sends a POST request to the server with the current system state.
 * - 0: System offline
 * - 1: Critical
 * - 2: System OK
 */
int send_status_update(int state) {
  CURL * curl = curl_easy_init();
  if (curl) {
    char post_data[50];
    snprintf(post_data, sizeof(post_data), "{\"state\": %d}", state);
    struct curl_slist * headers = curl_slist_append(NULL, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, ({
      char url[256];snprintf(url, sizeof(url), "%s/system/state", API_ENDPOINT);url;
    }));
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
  }
  return 0;
}


size_t health_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    struct ResponseData *response = (struct ResponseData *)userp;
    size_t realsize = size * nmemb;
    
    response->data = realloc(response->data, response->size + realsize + 1);
    if(response->data == NULL) {
        return 0;
    }
    /*
    This is rly not a great idea but its cool
    basically you can get the settings from the server in the response code
    means we don't need to have a returning api and the Pi doesn't have to have a known endpoint */
    memcpy(&(response->data[response->size]), contents, realsize);
    response->size += realsize;
    response->data[response->size] = 0;
    char *line = strtok(response->data, "\n");
    while (line != NULL) {
        if (strncmp(line, "checkInterval=", 14) == 0) {
            CHECK_INTERVAL = atoi(line + 14);  
        }
        else if (strncmp(line, "statusInterval=", 15) == 0) {
            STATUS_INTERVAL = atoi(line + 15);  
        }
        line = strtok(NULL, "\n"); 
    }
    return realsize;
}

int send_health_ping() {
    CURL *curl;
    CURLcode res;
    struct ResponseData response = {0};
    response.data = malloc(1); 
    response.size = 0;

    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, ({
            char url[256];
            snprintf(url, sizeof(url), "%s/health", API_ENDPOINT);
            url;
        }));
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, health_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response);

        res = curl_easy_perform(curl);
        if(res != CURLE_OK) 
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    
        curl_easy_cleanup(curl);
        free(response.data);
    }
    return (res == CURLE_OK) ? 0 : 1;
}


int main() {
    time_t last_send_time = 0;
    time_t last_health_ping_time = 0;

    if (wiringPiSetup() == -1) {
        fprintf(stderr, "WiringPi setup failed\n");
        return 1;
    }

    for (int i = 0; i < sizeof(PIN) / sizeof(PIN[0]); i++) 
        pinMode(PIN[i], INPUT);
    
    pullUpDnControl(PIN[3], PUD_DOWN); 
    pullUpDnControl(PIN[4], PUD_DOWN);   
    printf("Monitoring started...\n");

    while (1) {
        time_t now = time(NULL);
        float pin_values[5];

        if (now - last_health_ping_time >= STATUS_INTERVAL) {
            send_health_ping();
            last_health_ping_time = now;
        }

        for (int i = 0; i < sizeof(PIN) / sizeof(PIN[0]); i++) 
            pin_values[i] = digitalRead(PIN[i]);
        
        if (now - last_send_time >= CHECK_INTERVAL) {
            last_send_time = now;
            send_data_request(pin_values);
           // printf("Sent sensor readings at %s\n", ctime(&now));
        }

        sleep(1);
    }

    return 0;
}
