#ifndef APP_H
#define APP_H

// C hooks called from main.c
#ifdef __cplusplus
extern "C" {
#endif
void app_init(void);
void app_step(void);
#ifdef __cplusplus
}
#endif

#endif // APP_H
