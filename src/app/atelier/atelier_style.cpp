#include "atelier_style.h"

namespace atelier {
Palette& Style() { static Palette p; return p; }
}
