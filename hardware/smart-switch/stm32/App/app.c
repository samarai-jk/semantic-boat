#include "app.h"
#include "main.h"

// C-callable hooks implemented in C++
extern void app_drivers_init(void);
extern void app_drivers_step(void);

void app_init(void)
{
    app_drivers_init();
}

void app_step(void)
{
    app_drivers_step();
}
