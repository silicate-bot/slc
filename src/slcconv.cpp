#include "slc/formats/v2.hpp"
#include <filesystem>
#include <fstream>
#define SLC_NO_DEFAULT
#include <slc/slc.hpp>

struct OldMeta {
  uint64_t seed;
  char reserved[56];
};

namespace fs = std::filesystem;

// https://en.cppreference.com/w/cpp/utility/variant/visit
template <class... Ts> struct overloads : Ts... {
  using Ts::operator()...;
};

int main() {
  std::string inputName;
  std::cout << "input file name: ";
  std::cin >> inputName;

  std::string outputName;
  std::cout << "output file name: ";
  std::cin >> outputName;
  std::cout << "\n\n";

  const fs::path in_path = fs::current_path() / inputName;
  size_t oldSize = fs::file_size(in_path);
  std::ifstream in(in_path, std::ios::binary);
  auto oldrep = slc::v2::Replay<OldMeta>::read(in);
  // oldrep->clearInputs();
  for (int i = 1; i < 161001; i++) {
    // oldrep->addInput(i, slc::v2::Input::InputType::Jump, false, true);
    // oldrep->addInput(i, slc::v2::Input::InputType::Jump, false, false);
  }
  if (oldrep.has_value()) {
    auto rr = oldrep.value();

    std::println("read slc2 replay; {} inputs", rr.length());

    std::println("------------------------------------");

    slc::v3::Replay<> r;

    r.m_meta.m_tps = oldrep->m_tps;
    slc::v3::ActionAtom a;

    using ActionType = slc::v3::Action::ActionType;

    uint64_t currentFrame = 0;
    for (const auto &input : oldrep->getInputs()) {
      if (input.m_button == slc::v2::Input::InputType::Skip) {
        currentFrame = input.m_frame;
        continue;
      }
      a.m_actions.push_back(
          slc::v3::Action(currentFrame, input.m_frame - currentFrame,
                          static_cast<ActionType>(input.m_button),
                          input.m_holding, input.m_player2));
      currentFrame = input.m_frame;
    }

    std::println("converted to slc3, adding atom with inputs");
    r.m_atoms.add(a);

    const fs::path out_path = fs::current_path() / outputName;
    {
      std::ofstream fd(out_path, std::ios::binary);
      if (auto result = r.write(fd); !result.has_value()) {
        std::println("failed to write: {}", result.error().m_message);
        return 1;
      }
    }

    size_t newSize = fs::file_size(out_path);

    std::println("OLD: {}b, NEW: {}b ({:.2f}% savings)", oldSize, newSize,
                 (1.0 - (double)newSize / (double)oldSize) * 100.0);

    std::println("------------------------------------");

    // verify correctness
    {
      std::ifstream fd(out_path, std::ios::binary);
      auto res = slc::v3::Replay<>::read(fd);
      if (res.has_value()) {
        auto final = res.value();
        std::println("read slc3 replay with {} atom(s)", final.m_atoms.count());

        const auto visitor = overloads{
            [](slc::v3::NullAtom &atom) {
              std::println("null atom with size {}", atom.size);
            },
            [&](slc::v3::ActionAtom &atom) {
              std::println("action atom with {} inputs", atom.m_actions.size());
              std::println("checking correctness...");

              for (size_t i = 0; i < atom.m_actions.size(); i++) {
                const auto &na = atom.m_actions[i];
                const auto &oa = oldrep->getInputs()[i];

                if (i < 0)
                  std::println("COMPARING ACTIONS: {} / {}, {} ({}) / {} ({}), "
                               "{} / {}, SWIFT : {}",
                               static_cast<int>(na.m_type),
                               static_cast<int>(oa.m_button), na.m_frame,
                               na.delta(), oa.m_frame, oa.m_delta, na.m_holding,
                               oa.m_holding, na.swift());

                if (na.m_frame != oa.m_frame) {
                  std::println("FRAME MISMATCH: got {}, expected {}",
                               na.m_frame, oa.m_frame);
                  return;
                }

                if (static_cast<int>(na.m_type) !=
                        static_cast<int>(oa.m_button) ||
                    na.m_holding != oa.m_holding ||
                    na.m_player2 != oa.m_player2) {
                  std::println("ACTION MISMATCH at frame {}", na.m_frame);
                  std::println("{} / {}, {} / {}, swift: {}",
                               static_cast<int>(na.m_type),
                               static_cast<int>(oa.m_button), na.m_holding,
                               oa.m_holding, na.swift());
                  return;
                }
              }

              std::println("replay perfectly converted with 100% parity");
            }};

        for (auto &atom : final.m_atoms.m_atoms) {
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
