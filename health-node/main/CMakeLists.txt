idf_component_register(
    SRCS "main.c"
         "app/app.c"
         "dsp/fft.c" "dsp/features.c" "dsp/baseline.c" "dsp/scoring.c" "dsp/tonal.c"
         "hal/display.c" "hal/buttons.c" "hal/audio_capture.c" "hal/audio_out.c" "hal/storage.c"
         "ui/ui.c"
         "net/mhn_net.c" "net/dashboard.c"
    INCLUDE_DIRS "." "app" "dsp" "hal" "ui" "net"
    PRIV_REQUIRES esp_lcd esp_driver_spi esp_driver_i2s esp_driver_gpio
                  esp_wifi esp_netif esp_event nvs_flash
                  esp_http_server esp_timer
)
