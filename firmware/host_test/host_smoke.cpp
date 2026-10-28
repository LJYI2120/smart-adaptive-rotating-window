#include <cassert>

void setup();
void loop();

int main() {
  setup();
  for (int index = 0; index < 5; ++index) {
    loop();
  }
  assert(true);
  return 0;
}
