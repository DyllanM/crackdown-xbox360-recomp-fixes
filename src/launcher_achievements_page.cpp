#include "launcher_achievements_page.h"

#include <cfloat>
#include <string>

#include <imgui.h>

#include <rex/ui/overlay/achievement_icon_cache.h>

#include "launcher_achievements.h"

namespace crackdown {
namespace {

constexpr float kIconSize = 44.0f;
constexpr float kIconGap = 12.0f;
constexpr ImVec4 kLockedIconTint = ImVec4(0.45f, 0.45f, 0.45f, 0.80f);

void DrawSummary(trg::Ui& ui, const AchievementTotals& totals) {
  const ImVec4 accent = ui.theme().accent;
  ImGui::PushFont(ui.fonts().semibold, 0.0f);
  ImGui::Text("%d / %d unlocked", totals.unlocked, totals.total);
  ImGui::SameLine();
  ImGui::TextColored(accent, "%dG / %dG", totals.gamerscore_earned, totals.gamerscore_total);
  ImGui::PopFont();

  const float fraction = totals.total > 0 ? float(totals.unlocked) / float(totals.total) : 0.0f;
  ImGui::PushStyleColor(ImGuiCol_PlotHistogram, accent);
  ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 6.0f * ui.scale()), "");
  ImGui::PopStyleColor();
  ui.Spacer(8);
}

void DrawRow(trg::Ui& ui, const AchievementEntry& entry, rex::ui::AchievementIconCache* icons) {
  const trg::Theme& theme = ui.theme();
  const float icon_size = kIconSize * ui.scale();
  const char* state = entry.unlocked ? "UNLOCKED" : "LOCKED";

  ImGui::PushID(static_cast<int>(entry.info.id));
  ImGui::BeginGroup();
  rex::ui::ImmediateTexture* icon = icons ? icons->GetIcon(entry.info) : nullptr;
  if (icon) {
    ImGui::ImageWithBg(reinterpret_cast<ImTextureID>(icon), ImVec2(icon_size, icon_size), ImVec2(0, 0), ImVec2(1, 1),
                       ImVec4(0, 0, 0, 0), entry.unlocked ? ImVec4(1, 1, 1, 1) : kLockedIconTint);
  } else {
    ImGui::Dummy(ImVec2(icon_size, icon_size));
  }
  ImGui::SameLine(0.0f, kIconGap * ui.scale());

  ImGui::BeginGroup();
  ImGui::PushFont(ui.fonts().semibold, 0.0f);
  ImGui::TextColored(entry.unlocked ? theme.text : theme.dim, "%s", entry.info.label.c_str());
  ImGui::PopFont();
  ImGui::SameLine();
  ImGui::TextColored(theme.accent, "%dG", static_cast<int>(entry.info.gamerscore));
  ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(state).x);
  ImGui::TextColored(entry.unlocked ? theme.accent : theme.dim, "%s", state);
  ImGui::PushStyleColor(ImGuiCol_Text, entry.unlocked ? theme.text : theme.dim);
  ImGui::TextWrapped("%s", AchievementDescription(entry).c_str());
  ImGui::PopStyleColor();
  ImGui::EndGroup();
  ImGui::EndGroup();

  ui.Spacer(4);
  ImGui::Separator();
  ImGui::PopID();
}

}  // namespace

void DrawAchievementsPage(trg::Ui& ui, const std::vector<AchievementEntry>& entries,
                          rex::ui::AchievementIconCache* icons) {
  DrawSummary(ui, TotalAchievements(entries));
  for (const AchievementEntry& entry : entries) {
    DrawRow(ui, entry, icons);
  }
}

}  // namespace crackdown
