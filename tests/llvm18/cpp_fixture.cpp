#include <cassert>
#include <cstdio>
#include <new>

static volatile unsigned observed;

struct Object {
  unsigned char contents[256];
  __attribute__((noinline)) explicit Object(bool fail) {
    if (fail) throw 17;
    for (unsigned i = 0; i < sizeof(contents); ++i) contents[i] = i;
  }
};

static __attribute__((noinline)) int exercise(bool fail) {
  try {
    Object *value = new Object(fail);
    observed = value->contents[173];
    delete value;
  } catch (int exception) {
    assert(exception == 17);
    return 1;
  }
  return 0;
}

int main() {
  assert(exercise(false) == 0);
  assert(exercise(true) == 1);
  unsigned char *array = new unsigned char[512];
  array[42] = 91;
  observed = array[42];
  delete[] array;
  std::puts("C++ new/delete, new[]/delete[], invoke and constructor cleanup passed");
}
