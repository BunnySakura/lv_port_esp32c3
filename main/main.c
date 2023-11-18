#include <sys/cdefs.h>
/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_timer.h"
#include "esp_freertos_hooks.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "lvgl_helpers.h"

#define LED_4 12
#define LED_5 13
#define LOW_LEVEL 0
#define HIGH_LEVEL 1

void lv_tick_task(void *arg) {
  (void) arg;
  lv_tick_inc(1);
}

_Noreturn void PrintChipInfo(void *params) {
  (void) params;
  /* Print chip information */
  esp_chip_info_t chip_info;
  uint32_t flash_size;
  esp_chip_info(&chip_info);
  printf("This is %s chip with %d CPU core(s), %s%s%s%s, ",
         CONFIG_IDF_TARGET,
         chip_info.cores,
         (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
         (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
         (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
         (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");

  unsigned major_rev = chip_info.revision / 100;
  unsigned minor_rev = chip_info.revision % 100;
  printf("silicon revision v%d.%d, ", major_rev, minor_rev);
  if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
    printf("Get flash size failed");
  }

  printf("%"
         PRIu32
         "MB %s flash\n", flash_size / (uint32_t) (1024 * 1024),
         (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

  printf("Minimum free heap size: %"
         PRIu32
         " bytes\n", esp_get_minimum_free_heap_size());

  while (true) {
    // 任务禁止主动返回
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

_Noreturn void BlinkLed(void *params) {
  (void) params;
  uint8_t level = LOW_LEVEL;
  gpio_reset_pin(LED_4);
  gpio_set_direction(LED_4, GPIO_MODE_OUTPUT); // Set the GPIO as a push/pull output
  gpio_reset_pin(LED_5);
  gpio_set_direction(LED_5, GPIO_MODE_OUTPUT); // Set the GPIO as a push/pull output
  ESP_LOGI("BlinkLed", "LED configuration completed.");

  while (true) {
    gpio_set_level(LED_4, level);
    ESP_LOGI("BlinkLed", "LED_4: %s!", level == HIGH_LEVEL ? "ON" : "OFF");
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    level = !level;

    gpio_set_level(LED_5, level);
    ESP_LOGI("BlinkLed", "LED_5: %s!", level == HIGH_LEVEL ? "ON" : "OFF");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

_Noreturn void app_main(void) {

  xTaskCreate(PrintChipInfo, "PrintChipInfo", 1024 * 4, NULL, 1, NULL);
  xTaskCreate(BlinkLed, "BlinkLed", 1024 * 4, NULL, 1, NULL);

  fflush(stdout);

  {
    TaskHandle_t print_chip_info_handle = xTaskGetHandle("PrintChipInfo");
    if (print_chip_info_handle != NULL) {
      vTaskDelete(print_chip_info_handle);
      ESP_LOGI("app_main", "Task PrintChipInfo delete.");
    }
  }

  /**
   * \brief Start LVGL demo.
   */
  lv_init();
  lvgl_driver_init();
  lv_color_t *buf1 =
      heap_caps_malloc(DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
  assert(buf1 != NULL);
  /* Use double buffered when not working with monochrome displays */
#ifndef CONFIG_LV_TFT_DISPLAY_MONOCHROME
  lv_color_t *buf2 = heap_caps_malloc(DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
  assert(buf2 != NULL);
#else
  static lv_color_t *buf2 = NULL;
#endif
  static lv_disp_draw_buf_t disp_buf;
  uint32_t size_in_px = DISP_BUF_SIZE;
  lv_disp_draw_buf_init(&disp_buf, buf1, buf2, size_in_px);
  lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = CONFIG_LV_HOR_RES_MAX;
  disp_drv.ver_res = CONFIG_LV_VER_RES_MAX;
  disp_drv.flush_cb = disp_driver_flush;
  disp_drv.draw_buf = &disp_buf;
  lv_disp_drv_register(&disp_drv);

  /* Register an input device when enabled on the menuconfig */
#if CONFIG_LV_TOUCH_CONTROLLER != TOUCH_CONTROLLER_NONE
  ESP_LOGI(TAG, "TOUCH CONTROLLER");
  lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.read_cb = touch_driver_read;
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  lv_indev_drv_register(&indev_drv);
#endif

  const esp_timer_create_args_t periodic_timer_args = {
      .callback = &lv_tick_task, .name = "screen"};
  esp_timer_handle_t periodic_timer;
  ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
  ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 1000));

  ESP_LOGI(__FILENAME__, "Free Heap Size: %lu", esp_get_minimum_free_heap_size());

  lv_obj_t *label = lv_label_create(lv_scr_act());
  if (NULL != label) {
    lv_label_set_text(label, "Hello world");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
  }

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(10));
    lv_task_handler();
  }

  free(buf1);
  free(buf2);
  vTaskDelete(NULL);
}