#include <gfx/gfx.h>

int main(void) {
  // Prints the adapter it picks
  GFX_Error err = gfx_graphics_init();
  gfx_graphics_terminate();
  return err == GFX_ERR_OK ? 0 : 1;
}
