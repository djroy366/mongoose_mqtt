// Copyright (c) 2023 Cesanta Software Limited
// All rights reserved

#include "hal.h"
#include "mongoose.h"

#define BLINK_PERIOD_MS 1000  // LED blinking period in millis

void mg_random(void *buf, size_t len) {  // Use on-board RNG
  for (size_t n = 0; n < len; n += sizeof(uint32_t)) {
    uint32_t r = rng_read();
    memcpy((char *) buf + n, &r, n + sizeof(r) > len ? len - n : sizeof(r));
  }
}

static void timer_fn(void *arg) {
  struct mg_tcpip_if *ifp = arg;                         // And show
  const char *names[] = {"down", "up", "req", "ready"};  // network stats
  MG_INFO(("Ethernet: %s, IP: %M, rx:%u, tx:%u, dr:%u, er:%u",
           names[ifp->state], mg_print_ip4, &ifp->ip, ifp->nrecv, ifp->nsent,
           ifp->ndrop, ifp->nerr));
}

static void fn(struct mg_connection *c, int ev, void *ev_data) {
  struct mg_tcpip_if *ifp = (struct mg_tcpip_if *) c->fn_data;
  if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *hm = (struct mg_http_message *) ev_data;
    if (mg_http_match_uri(hm, "/api/hello")) {  // Request to /api/hello
      mg_http_reply(c, 200, "", "{%m:%u,%m:%u,%m:%u,%m:%u,%m:%u}\n",
                    MG_ESC("eth"), ifp->state, MG_ESC("frames_received"),
                    ifp->nrecv, MG_ESC("frames_sent"), ifp->nsent,
                    MG_ESC("frames_dropped"), ifp->ndrop,
                    MG_ESC("interface_errors"), ifp->nerr);
    } else if (mg_http_match_uri(hm, "/")) {  // Index page
      mg_http_reply(
          c, 200, "", "%s",
          "<html><head><link rel='icon' href='data:;base64,='></head><body>"
          "<h1>Welcome to Mongoose</h1>"
          "See <a href=/api/hello>/api/hello</a> for REST example"
          "</body></html>");
    } else {  // All other URIs
      mg_http_reply(c, 404, "", "Not Found\n");
    }
  }
}

static void server(void *args) {
  struct mg_mgr mgr;        // Initialise Mongoose event manager
  mg_mgr_init(&mgr);        // and attach it to the interface
  mg_log_set(MG_LL_DEBUG);  // Set log level

  // Initialise Mongoose network stack
  ethernet_init();
  struct mg_tcpip_driver_imxrt_data driver_data = {.mdc_cr = 24, .phy_addr = 2};
  struct mg_tcpip_if mif = {.mac = GENERATE_LOCALLY_ADMINISTERED_MAC(),
                            // Uncomment below for static configuration:
                            // .ip = mg_htonl(MG_U32(192, 168, 0, 223)),
                            // .mask = mg_htonl(MG_U32(255, 255, 255, 0)),
                            // .gw = mg_htonl(MG_U32(192, 168, 0, 1)),
                            .driver = &mg_tcpip_driver_imxrt,
                            .driver_data = &driver_data};
  mg_tcpip_init(&mgr, &mif);
  mg_timer_add(&mgr, BLINK_PERIOD_MS, MG_TIMER_REPEAT, timer_fn, &mif);

  MG_INFO(("MAC: %M. Waiting for IP...", mg_print_mac, mif.mac));
  while (mif.state != MG_TCPIP_STATE_READY) {
    mg_mgr_poll(&mgr, 0);
  }

  MG_INFO(("Initialising application..."));
  mg_http_listen(&mgr, "http://0.0.0.0:80", fn, &mif);

  MG_INFO(("Starting event loop"));
  for (;;) mg_mgr_poll(&mgr, 1);  // Infinite event loop
  (void) args;
}

static void blinker(void *args) {
  gpio_output(LED);  // Setup blue LED
  for (;;) {
    gpio_toggle(LED);
    vTaskDelay(pdMS_TO_TICKS(BLINK_PERIOD_MS));
  }
  (void) args;
}

int main(void) {
  uart_init(UART_DEBUG, 115200);  // Initialise UART

  // Start tasks. NOTE: stack sizes are in 32-bit words
  xTaskCreate(blinker, "blinker", 128, ":)", configMAX_PRIORITIES - 1, NULL);
  xTaskCreate(server, "server", 2048, 0, configMAX_PRIORITIES - 1, NULL);

  vTaskStartScheduler();  // This blocks
  return 0;
}
