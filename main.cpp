#include <daisy.h>
#include "app.h"

const char* USBD_MANUFACTURER_STRING = "Synthux";
const char* USBD_PRODUCT_STRING_HS = "Spotykach";
const char* USBD_PRODUCT_STRING_FS = "Spotykach";

static spotykach::Application app;

extern "C" int main(void)
{
    app.Init();
    app.Loop();
}
