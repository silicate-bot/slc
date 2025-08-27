#include "slc/formats/v2.hpp"
#include "slc/formats/v3/atom.hpp"
#include "slc/formats/v3/builtin.hpp"
#include <chrono>
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

static void convertSlc2ToSlc3(std::string inputName, std::string outputName) {
  const fs::path in_path = fs::current_path() / inputName;
  size_t oldSize = fs::file_size(in_path);
  std::ifstream in(in_path, std::ios::binary);
  auto oldrep = slc::v2::Replay<OldMeta>::read(in);
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

      if (static_cast<int>(input.m_button) <
          static_cast<int>(slc::v2::Input::InputType::Restart)) {
        a.m_actions.push_back(
            slc::v3::Action(currentFrame, input.m_frame - currentFrame,
                            static_cast<ActionType>(input.m_button),
                            input.m_holding, input.m_player2));
        currentFrame = input.m_frame;
        continue;
      }

      if (static_cast<int>(input.m_button) <
          static_cast<int>(slc::v2::Input::InputType::TPS)) {
        a.m_actions.push_back(slc::v3::Action(
            currentFrame, input.m_frame - currentFrame,
            static_cast<ActionType>(input.m_button), oldrep->m_meta.seed));
        currentFrame = input.m_frame;
        continue;
      }

      a.m_actions.push_back(slc::v3::Action(
          currentFrame, input.m_frame - currentFrame, input.m_tps));
      currentFrame = input.m_frame;
    }

    std::println("converted to slc3, adding atom with inputs");
    r.m_atoms.add(std::move(a));

    std::chrono::high_resolution_clock clock;
    auto startW = clock.now();

    const fs::path out_path = fs::current_path() / outputName;
    {
      std::println("writing slc3 replay...");
      std::ofstream fd(out_path, std::ios::binary);
      if (auto result = r.write(fd); !result.has_value()) {
        std::println("failed to write: {}", result.error().m_message);
        return;
      }
    }

    auto endW = clock.now();

    std::println(
        "wrote in {}",
        std::chrono::duration_cast<std::chrono::milliseconds>(endW - startW));

    size_t newSize = fs::file_size(out_path);

    std::println("OLD: {}b, NEW: {}b ({:.2f}% savings)", oldSize, newSize,
                 (1.0 - (double)newSize / (double)oldSize) * 100.0);

    std::println("------------------------------------");

    // verify correctness
    {
      std::ifstream fd(out_path, std::ios::binary);
      auto startR = clock.now();
      auto res = slc::v3::Replay<>::read(fd);
      if (res.has_value()) {
        auto final = res.value();
        auto endR = clock.now();
        std::println("read slc3 replay with {} atom(s)", final.m_atoms.count());
        std::println("read in {}",
                     std::chrono::duration_cast<std::chrono::milliseconds>(
                         endR - startR));

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

static void convertSlc3ToSlc2(std::string inputName, std::string outputName) {
  const fs::path in_path = fs::current_path() / inputName;
  // size_t oldSize = fs::file_size(in_path);
  std::ifstream in(in_path, std::ios::binary);
  auto oldrep = slc::v3::Replay<>::read(in);
  if (oldrep.has_value()) {
    auto rr = oldrep.value();

    auto it = std::find_if(
        rr.m_atoms.m_atoms.begin(), rr.m_atoms.m_atoms.end(), [](auto &v) {
          return std::visit(
              [](auto &atom) { return atom.id == slc::v3::AtomId::Action; }, v);
        });

    slc::v3::ActionAtom atom = std::get<slc::v3::ActionAtom>(*it);

    std::println("read slc3 replay; {} inputs", atom.length());

    std::println("------------------------------------");

    slc::v2::Replay<OldMeta> r;

    r.m_tps = oldrep->m_meta.m_tps;
    for (size_t i = 0; i < atom.m_actions.size(); i++) {
      const auto &a = atom.m_actions[i];

      if (a.m_type == slc::v3::Action::ActionType::TPS) {
        (void)r.addTPSInput(a.m_frame, a.m_tps);
      } else {
        (void)r.addInput(a.m_frame,
                         static_cast<slc::v2::Input::InputType>(a.m_type),
                         a.m_player2, a.m_holding);
      }
    }

    r.m_meta.seed = rr.m_meta.m_seed;

    const fs::path out_path = fs::current_path() / outputName;
    {
      std::println("writing slc2 replay...");
      std::ofstream fd(out_path, std::ios::binary);
      r.write(fd);
    }

  } else {
    std::println("exited with {}", oldrep.error().m_message);
  }
}

int main() {
  std::string inputName;
  std::cout << "input file name: ";
  std::cin >> inputName;

  std::string outputName;
  std::cout << "output file name: ";
  std::cin >> outputName;
  std::cout << "\n\n";

  int mode;
  std::cout << "Mode (0 - slc2 to slc3, 1 - slc3 to slc2): ";
  std::cin >> mode;
  std::cout << "\n\n";

  if (mode == 1) {
    convertSlc3ToSlc2(inputName, outputName);
  } else {
    convertSlc2ToSlc3(inputName, outputName);
  }
}
