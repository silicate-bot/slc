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

// https://en.cppreference.com/w/cpp/utility/variant/visit
template<class... Ts>
struct overloads : Ts... { using Ts::operator()...; };

int main() {
  const fs::path in_path = fs::current_path() / "in.slc";
  size_t oldSize = fs::file_size(in_path);
  std::ifstream in(in_path, std::ios::binary);
  auto oldrep = slc::v2::Replay<OldMeta>::read(in);
  if (oldrep.has_value()) {
    auto rr = oldrep.value();

    std::println("read slc2 replay; {} inputs", rr.length());

    std::println("------------------------------------");

    slc::v3::Replay<> r;
    slc::v3::ActionAtom a;

    using ActionType = slc::v3::Action::ActionType;

    uint64_t currentFrame = 0;
    for (const auto &input : oldrep->getInputs()) {
      a.m_actions.push_back(
          slc::v3::Action(currentFrame, input.m_frame - currentFrame,
                          static_cast<ActionType>(input.m_button),
                          input.m_holding, input.m_player2));

      currentFrame = input.m_frame;
    }

    std::println("converted to slc3, adding atom with inputs");
    r.m_atoms.add(a);

    const fs::path out_path = fs::current_path() / "out.slc";
    {
      std::ofstream fd("out.slc", std::ios::binary);
      r.write(fd);
    }

    size_t newSize = fs::file_size(out_path);

    std::println("OLD: {}b, NEW: {}b ({:.2f}\% savings)", oldSize, newSize, (1.0 - (double)newSize / (double)oldSize)*100.0);

    std::println("------------------------------------");

    // verify correctness
    {
	std::ifstream fd(out_path, std::ios::binary);
	auto res = slc::v3::Replay<>::read(fd);
	if (res.has_value()) {
	    auto final = res.value();
	    std::println("read slc3 replay with {} atom(s)", final.m_atoms.count());

	    const auto visitor = overloads {
		[](slc::v3::NullAtom& atom){ std::println("null atom with size {}", atom.size); },
		[](slc::v3::ActionAtom& atom){ std::println("action atom with {} inputs", atom.m_actions.size()); }
	    };

	    for (auto& atom : final.m_atoms.m_atoms) {
		std::visit(visitor, atom);
	    }
	} else {
	    std::println("re-reading failed with {}", res.error().m_message);
	}
    }
  } else {
    std::println("exited with {}", static_cast<int>(oldrep.error()));
  }
}
