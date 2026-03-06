#include "app/app_bootstrap.h"

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE previous_instance, PWSTR command_line, int command_show) {
  (void)previous_instance;
  (void)command_line;

  velox::app::AppBootstrap bootstrap(instance, command_show);
  return bootstrap.Run();
}
