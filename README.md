# ktcoro_wait

Suspending function execute by time based on C++20 Coroutines

## Usage

Clone repository and simply include ktcoro_wait.hpp. C++20 compatible compiler required

## Example

```cpp
#include <iostream>
#include <chrono>

#include "ktcoro_wait.hpp"

ktwait bar() {
    using namespace std::chrono_literals;
    while (true) {
        std::cout << "I am called every 2.5 seconds" << std::endl;
        co_await 2.5s;
    }
}

ktwait baz() {
    using namespace std::chrono_literals;
    co_await 3s;
    std::cout << "hello from baz after 3 seconds" << std::endl;
}

ktwait foo(int time) {
    while (true) {
        co_await std::chrono::seconds(time);
        std::cout << "I am calling baz every " << time << " seconds" << std::endl;
        co_await baz();
        std::cout << "I am not waiting for baz" << std::endl;
    }
}

int main() {
    foo(2);
    bar();
    while (true) {
        ktcoro_wait::Instance().process();
    }
}
```