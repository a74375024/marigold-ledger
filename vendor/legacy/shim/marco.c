// marigold: see marco.h.
#include "marco.h"
#include <stdio.h>
void marco(const char *texto) {
  printf("[marco] %s\n", texto ? texto : "");
  fflush(stdout);
}
