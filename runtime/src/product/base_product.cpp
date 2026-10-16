#include "runtime_product.h"

namespace RuntimeProduct {

const Descriptor& Active() noexcept {
    static constexpr Descriptor descriptor{
        Kind::BaseGame,
        "Sonic and the Black Knight",
    };
    return descriptor;
}

} // namespace RuntimeProduct
