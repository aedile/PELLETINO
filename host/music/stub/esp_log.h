#pragma once
#include <stdio.h>
#define ESP_LOGI(t, ...) do { printf("I "); printf(__VA_ARGS__); printf("\n"); } while (0)
#define ESP_LOGW(t, ...) do { printf("W "); printf(__VA_ARGS__); printf("\n"); } while (0)
