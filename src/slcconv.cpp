#include <fstream>
#include <iostream>
#include <slc/slc.hpp>

#include <algorithm>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <print>
#include <string>
#include <vector>

enum class FileError {
  ReadError,
};

template <typename T>
[[nodiscard]] std::expected<T, FileError> file_read(std::istream &s) {
  T value;

  s.read(reinterpret_cast<char *>(&value), sizeof(T));

  return value;
}

template <typename T>
std::expected<void, FileError> file_read_arr(std::istream &s, T *out,
                                             size_t len) {
  s.read(reinterpret_cast<char *>(out), sizeof(T) * len);

  return {};
}

struct OldReplay {
  enum class Error {
    FileError,
    ReadError,
  };

  enum class InputType : uint8_t { Reserved = 0, Click, Left, Right };

  struct alignas(4) Input {
    static constexpr uint32_t FRAME_MASK = 0xfffffff0;
    static constexpr uint32_t PLAYER2_MASK = 0b1000;
    static constexpr uint32_t BUTTON_MASK = 0b110;
    static constexpr uint32_t HOLDING_MASK = 0b1;

    uint32_t m_state;

    uint32_t m_frame = 0;
    bool m_player2 = false;
    InputType m_button = InputType::Reserved;
    bool m_holding = false;

    Input(uint32_t frame, bool p2, uint8_t btn, bool pressed) {
      m_state = (frame << 4) | (p2 << 3) | (btn << 1) | pressed;
      m_frame = frame;
      m_player2 = p2;
      m_button = static_cast<InputType>(btn);
      m_holding = pressed;
    }
    Input(uint32_t state) : m_state(state) {
      m_frame = (state & FRAME_MASK) >> 4;
      m_player2 = (state & PLAYER2_MASK) >> 3;
      m_button = static_cast<InputType>((state & BUTTON_MASK) >> 1);
      m_holding = state & HOLDING_MASK;
    }
    Input() = default;
  };

  std::vector<Input> m_inputs;
  double m_fps = 240.0;
  size_t m_seed;

  const double fps() const { return m_fps; }
  const uint32_t length() const { return m_inputs.size(); }

  OldReplay() = default;
  OldReplay(double fps) : m_fps(fps) {}

  static constexpr size_t BUFFER_SIZE = 8192;

  [[nodiscard]] static std::expected<OldReplay, Error>
  fromFile(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);

    OldReplay replay;

    replay.m_fps = file_read<double>(file).value_or(240.0);

    uint32_t inputCount = file_read<uint32_t>(file).value();

    replay.m_inputs.resize(inputCount);
    for (auto &input : replay.m_inputs) {
      uint32_t state = file_read<uint32_t>(file).value();
      input = OldReplay::Input(state);
    }

    replay.m_seed = file_read<size_t>(file).value_or(69696969);

    return replay;
  }

  std::expected<std::vector<uint8_t>, Error> toBytes();
  std::expected<void, Error> toFile(const std::filesystem::path &path);

  inline void addInput(const Input input) { m_inputs.emplace_back(input); }
  inline void addInput(const Input &&input) { m_inputs.emplace_back(input); }
};

struct ReplayMeta {
  uint64_t m_seed;
  char _reserved[56];
};

void convert(std::string& in, std::string& out) {
  slc::Replay<ReplayMeta> replay;
  OldReplay oldReplay;
  int oldReplaySize = std::filesystem::file_size(in);
  OldReplay from = OldReplay::fromFile(in).value();

  if (in.front() == '"' && in.back() == '"') {
    in.substr(1, in.size() - 2);
  }

  if (out.front() == '"' && out.back() == '"') {
    out.substr(1, in.size() - 2);
  }

  replay.m_tps = from.fps();
  replay.m_meta = {from.m_seed};

  std::cout << "Converting replay...\n";

  for (const auto &input : from.m_inputs) {
    replay.addInput(input.m_frame,
                    static_cast<slc::Input::InputType>(input.m_button),
                    input.m_player2, input.m_holding);
  }

  auto start = std::chrono::high_resolution_clock::now();
  {
    std::ofstream file(out, std::ios::binary);
    replay.write(file);
  }
  auto end = std::chrono::high_resolution_clock::now();
  std::cout << "Converted replay successfully written to: " << out << "\n";

  std::cout << "Conversion took: "
            << std::chrono::duration_cast<std::chrono::milliseconds>(end -
                                                                     start)
                   .count()
            << "ms\n";
  int newReplaySize = std::filesystem::file_size(out);
  std::println("Replay size: {}b ({}b savings)", newReplaySize,
               oldReplaySize - newReplaySize);

}

int main() {

    std::cout << "Input legacy replay path: ";
    std::string path;
    std::getline(std::cin >> std::ws, path);

    if (path.front() == '"' && path.back() == '"') {
	path = path.substr(1, path.size() - 2);
    }

    if (std::filesystem::is_directory(path)) {
	const std::filesystem::path dir = path;

	std::cout << "Output directory path: ";
	std::string path;
	std::getline(std::cin >> std::ws, path);

	if (path.front() == '"' && path.back() == '"') {
	    path = path.substr(1, path.size() - 2);
	}

	const std::filesystem::path out = path;
	std::filesystem::create_directory(out);

	for (const auto& entry : std::filesystem::directory_iterator{dir}) {
	    if (entry.path().extension() != ".slc") {
		std::cout << entry.path().filename() << " not an slc1 replay, skipping...\n";
		continue;
	    }

	    std::filesystem::path outPath = 
		std::filesystem::path(out / entry.path().filename());

	    std::string in = entry.path().string();
	    std::string outs = outPath.string();

	    convert(in, outs);
	}
    } else {
	std::string in = path;
	std::cout << "Output slc2 replay path: ";
	std::string path;
	std::getline(std::cin >> std::ws, path);

	if (path.front() == '"' && path.back() == '"') {
	    path = path.substr(1, path.size() - 2);
	}
	
	convert(in, path);
    }

    return 0;
}
