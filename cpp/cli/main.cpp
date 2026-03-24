#include "stippling/engine/engine.hpp"

#include <iostream>

int main() {
  stippling::Engine engine;

  std::cout << "stippling_cli backend status: " << engine.status_string()
            << '\n';
  std::cout << "incremental_fitness="
            << engine.capabilities().incremental_fitness << '\n';
  std::cout << "multiscale=" << engine.capabilities().multiscale << '\n';

  return 0;
}
