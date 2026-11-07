#pragma once
#include <string>
#include <string_view>
namespace aura::l10n {
std::string_view transfer_page();
// The pages the owner's phone sees while it signs the device in to
// aoraton.com (ADR 0010): Greek with English beneath, and no script. Wait
// asks again every second; Google moves on to `google_url` (escaped here) and
// links to it as well. The others end the phone's part.
enum class PhoneSignInPage { Wait, Google, Done, Cancelled, Gone };
std::string phone_sign_in_page(PhoneSignInPage page, std::string_view google_url = {});
}  // namespace aura::l10n
