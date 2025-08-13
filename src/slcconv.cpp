#include <filesystem>
#include <fstream>
#define SLC_NO_DEFAULT
#define SLC_INSPECT
#include <slc/slc.hpp>

struct OldMeta {
  uint64_t seed;
  char reserved[56];
};

namespace fs = std::filesystem;

int main() {
  const fs::path in_path = fs::current_path() / "in.slc";
  size_t oldSize = fs::file_size(in_path);
  std::ifstream in(in_path, std::ios::binary);
  auto oldrep = slc::v2::Replay<OldMeta>::read(in);
  if (oldrep.has_value()) {
    auto rr = oldrep.value();

    std::println("read {} inputs", rr.length());

    slc::v3::Replay r;

    using ActionType = slc::v3::Action::ActionType;

    uint64_t currentFrame = 0;

    for (const auto &input : oldrep->getInputs()) {
      r.m_actions.push_back(
          slc::v3::Action(currentFrame, input.m_frame - currentFrame,
                          static_cast<ActionType>(input.m_button),
                          input.m_holding, input.m_player2));

      currentFrame = input.m_frame;
    }

    const fs::path out_path = fs::current_path() / "out.slc";
    {
      std::ofstream fd("out.slc", std::ios::binary);
      r.write(fd);
    }

    size_t newSize = fs::file_size(out_path);

    std::println("OLD: {}b, NEW: {}b", oldSize, newSize);
  } else {
    std::println("exited with {}", static_cast<int>(oldrep.error()));
  }
}
