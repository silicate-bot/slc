#include <fstream>
#include <iostream>
#include <slc/slc.hpp>

int main() {
  slc::Replay<int> replay;

  replay.m_meta = 0;

  replay.m_inputs.push_back(
      slc::Input(49, slc::Input::InputType::Jump, false, true));
  replay.m_inputs.push_back(
      slc::Input(24512, slc::Input::InputType::Jump, false, false));

  replay.m_inputs.push_back(slc::Input(700000.0)); // TPS input

  replay.m_inputs.push_back(
      slc::Input(21472040343, slc::Input::InputType::Restart, false, true));

  for (int i = 0; i < 5; i++) {
    replay.m_inputs.push_back(
        slc::Input(2, slc::Input::InputType::Jump, false, true));
    replay.m_inputs.push_back(
        slc::Input(2, slc::Input::InputType::Jump, false, false));
  }

  {
    auto s = std::ofstream("replay.slc");
    replay.write(s);
  }

  auto is = std::ifstream("replay.slc");

  slc::Replay<int> written = slc::Replay<int>::read(is).value();
  for (const auto &input : written.m_inputs) {
    if (input.type() != slc::Input::InputType::TPS) {
      std::cout << input.delta() << " " << static_cast<uint32_t>(input.type())
                << " " << input.player2() << " " << input.hold() << "\n";
    } else {
      std::cout << "tps: " << input.tps() << "\n";
    }
  }
}
