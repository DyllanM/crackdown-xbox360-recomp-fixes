#include <atomic>

#include <rex/hook.h>
#include <rex/logging.h>

#include "language_setting.h"
#include "settings.h"

REX_EXTERN(__imp__sub_823819A0);

namespace {

// Return address of the call in sub_823225C0 that applies the language the console reports, used when
// D:\language.ini is absent.
constexpr uint32_t kConsoleLanguageSite = 0x8232293C;

}  // namespace

// Other callers of sub_823819A0 keep the original call.
REX_HOOK_RAW(sub_823819A0) {
  if (uint32_t(ctx.lr) == kConsoleLanguageSite) {
    static std::atomic<bool> logged{false};
    const crackdown::GameLanguage& language = crackdown::LanguageFor(REXCVAR_GET(crackdown_language));
    ctx.r3.u64 = language.title_index;
    if (!logged.exchange(true)) {
      REXLOG_INFO("Language: {} ({})", language.label, language.key);
    }
  }
  __imp__sub_823819A0(ctx, base);
}
