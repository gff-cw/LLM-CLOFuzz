#include <cstddef>
extern void escape(void *);
__attribute__((noinline)) void cpp_memory_fixture(std::size_t size) {
  char *single = new char;
  escape(single);
  char *array = new char[size];
  escape(array);
  delete single;
  delete[] array;
}
