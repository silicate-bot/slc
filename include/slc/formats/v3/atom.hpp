#ifndef _SLC_V3_ATOM_HPP
#define _SLC_V3_ATOM_HPP

#include "slc/formats/v3/error.hpp"
#include "slc/util.hpp"

#include <array>
#include <concepts>
#include <ostream>
#include <type_traits>
#include <variant>
#include <vector>

SLC_NS_BEGIN

namespace v3 {

enum class AtomId : uint32_t {
  Null = 0,
  Action = 1,
  Marker = 2,
};

template <typename T>
concept IsAtom =
    requires(T &t, std::istream &is, std::ostream &os, size_t size) {
      { T::id } -> std::convertible_to<AtomId>;
      { t.size } -> std::convertible_to<size_t>;

      { t.write(os) } -> std::same_as<Result<>>;
      { T::read(is, size) } -> std::same_as<Result<T>>;
    };

struct NullAtom {
  static inline constexpr AtomId id = AtomId::Null;
  size_t size;

  static Result<NullAtom> read(std::istream &in, size_t size) {
    // skip reading data
    in.seekg(size, std::ios::cur);

    return NullAtom{
        .size = size,
    };
  }

  Result<> write([[maybe_unused]] std::ostream &out) const { return {}; }
};

template <IsAtom... Ts> struct AtomSerializer {
  using Self = AtomSerializer<Ts...>;
  using Variant = std::variant<Ts...>;
  using AtomIdT = std::underlying_type_t<AtomId>;
  using Read = Result<Variant> (*)(std::istream &, size_t);

  static consteval auto constructLookup() {
    // AtomIds should be continuous and in ascending order; this is perfectly
    // legal to do
    std::array<Read, sizeof...(Ts)> lookup{};

    ((lookup[static_cast<AtomIdT>(Ts::id)] = &wrap<Ts>), ...);
    return lookup;
  }

  template <IsAtom T>
  static Result<Variant> wrap(std::istream &in, size_t size) {
    auto r = T::read(in, size);
    if (r.has_value()) {
      return Variant{std::move(r.value())};
    }

    return r; // should be std::string
  }

  static constexpr auto lookup = constructLookup();

  static Result<Variant> read(std::istream &in, AtomId id, size_t size,
                              uint8_t) {
    AtomIdT idx = static_cast<AtomIdT>(id);

    // default to NullAtom if parser doesn't recognize atom type
    // this is incredibly useful for defining custom atoms
    if (idx >= lookup.size())
      return NullAtom::read(in, size);
    if (auto reader = lookup[idx])
      return reader(in, size);

    return std::unexpected("invalid atom passed to reader");
  }

  static Result<Variant> read(std::istream &in) {
    AtomIdT id = util::binRead<AtomIdT>(in);
    size_t size = util::binRead<uint64_t>(in);

    uint8_t flags = size >> 56;
    size &= ~(0xFFull << 56);

    // Check remaining size
    const auto currentPos = in.tellg();
    if (currentPos == -1) {
      return std::unexpected("failed to query current position");
    }

    const auto endPos = in.seekg(0, std::ios::end).tellg();
    if (endPos == -1) {
      return std::unexpected("failed to query end position");
    }

    in.seekg(currentPos, std::ios::beg);

    if (static_cast<size_t>(endPos - currentPos) < size) {
      return std::unexpected("atom size exceeds remaining stream size");
    }

    return Self::read(in, static_cast<AtomId>(id), size, flags);
  }

  static Result<> write(std::ostream &out, Variant &a) {
    return std::visit(
        [&](auto &atom) -> Result<> {
          util::binWrite(out, atom.id);

          auto before = out.tellp();
          if (before == -1) {
            return std::unexpected("failed to query before position");
          }

          util::binWrite(out, 0ull);

          auto start = out.tellp();
          if (start == -1) {
            return std::unexpected("failed to query start position");
          }
          atom.write(out);

          auto end = out.tellp();
          if (end == -1) {
            return std::unexpected("failed to query end position");
          }

          atom.size = end - start;

          out.seekp(before, std::ios::beg);

          util::binWrite<uint64_t>(out, atom.size);
          out.seekp(end, std::ios::beg);

          return {};
        },
        a);
  }
};

template <IsAtom... Ts> class AtomRegistry {
public:
  using Variant = std::variant<Ts...>;
  using Serializer = AtomSerializer<Ts...>;

  template <typename T> static constexpr AtomId getId = T::id;

public:
  std::vector<Variant> m_atoms;

  void add(const Variant &v) { m_atoms.push_back(v); }
  void add(Variant &&v) { m_atoms.push_back(v); }

  size_t count() const { return m_atoms.size(); }

  Result<> readAll(std::istream &in) {
    auto pos = in.tellg();
    if (pos == -1) {
      return std::unexpected("failed to query position");
    }

    in.seekg(0, std::ios::end);
    auto end = in.tellg();
    if (end == -1) {
      return std::unexpected("failed to query end position");
    }

    in.seekg(pos, std::ios::beg);
    end -= 1; // subtract one for footer length

    while (in.tellg() < end) {
      auto result = TRY(Serializer::read(in));
      this->add(result);
    }

    return {};
  }

  void writeAll(std::ostream &out) {
    for (auto &atom : m_atoms) {
      Serializer::write(out, atom);
    }
  }
};

} // namespace v3

SLC_NS_END

#endif
