#include <basic_coro/defer.hpp>
#include "check.h"


int main(int argc, char **) {
    bool processed = false;
    coro::defer([tst = argc, &argc, &processed]{

        coro::defer([&processed]{
            processed = true;
        });

        CHECK_EQUAL(tst ,argc);
        CHECK(!processed);

    });
    CHECK(processed);
    
    processed = false;

    coro::enter_defer_context([&processed]{
        std::string h = "hello world";
        coro::defer([h, &processed]{
            CHECK_EQUAL(h, "hello world");
            processed = true;
        });
        CHECK(!processed);
    });
    CHECK(processed);

}