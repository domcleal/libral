#include <libral/provider.hpp>

#include <leatherman/locale/locale.hpp>

using namespace leatherman::locale;

namespace libral {

  /*
   * implementation for attr_map
   */

  const value attr_map::operator[](const std::string& key) const {
    auto v = find(key);
    if (v == end()) {
      return boost::none;
    } else {
      return v->second;
    }
  }

  value& attr_map::operator[](const std::string& key) {
    return std::map<std::string, value>::operator[](key);
  }

  template<typename T>
  const T& attr_map::lookup(const std::string& key, const T& deflt) const {
    auto it = find(key);
    if (it == end()) {
      return deflt;
    } else if (auto p = it->second.as<T>()) {
      return *p;
    } else {
      return deflt;
    }
  }

  template<typename T>
  const T* attr_map::lookup(const std::string& key) const {
    auto it = find(key);
    if (it == end()) {
      return nullptr;
    } else {
      return it->second.as<T>();
    }
  }

  template const std::string& attr_map::lookup(const std::string&, const std::string&) const;
  template const std::string* attr_map::lookup(const std::string&) const;

  static bool is_name(const std::string& key) {
    // This basically hardcodes namevar for now
    return key == "name";
  }

  /*
   * implementation for changes
   */

  void changes::add(const std::string &attr, const value &is,
                    const value &was) {
    push_back(change(attr, is, was));
  }

  bool changes::exists(const std::string &attr) {
    for (auto ch : (*this)) {
      if (ch.attr == attr)
        return true;
    }
    return false;
  }

  std::ostream& operator<<(std::ostream& os, changes const& chgs) {
    for (auto chg: chgs) {
      auto was = chg.was.to_string();
      auto is = chg.is.to_string();
      os << chg.attr << "(" << was << "->" << is << ")" << std::endl;
    }
    return os;
  }

  /*
   * implementation for resource
   */
  const value resource::operator[](const std::string& key) const {
    if (is_name(key)) {
      throw std::invalid_argument {
        "The name can not be accessed with operator[]" };
    }

    return _attrs[key];
  }

  value& resource::operator[](const std::string& key) {
    if (is_name(key)) {
      throw std::invalid_argument {
        "The name can not be accessed with operator[]" };
    }
    return _attrs[key];
  }

  template<typename T>
  const T& resource::lookup(const std::string& key, const T& deflt) const {
    return _attrs.lookup<T>(key, deflt);
  }

  template<typename T>
  const T* resource::lookup(const std::string& key) const {
    return _attrs.lookup<T>(key);
  };

  template const std::string& resource::lookup(const std::string&, const std::string&) const;
  template const std::string* resource::lookup(const std::string&) const;

  void resource::check(changes& chgs, const attr_map& should,
                       const std::vector<std::string>& props) {
    auto& is = *this;
    for (auto prop : props) {
      if (should[prop].is_present() && (is[prop] != should[prop])) {
        chgs.add(prop, should[prop], is[prop]);
      }
    }
  }

  /*
   * Implementation for provider
   */
  boost::optional<std::unique_ptr<resource>>
  provider::find(const std::string &name) {
    for (auto& inst : instances()) {
      if (inst->name() == name)
        return std::move(inst);
    }
    return boost::none;
  }

  result<value> provider::parse(const std::string& name, const std::string& v) {
    if (!_spec) {
      return error(_("internal error: spec was not initialized"));
    }
    auto spec = _spec->attr(name);
    if (!spec) {
      return error(_("there is no attribute '{1}'", name));
    }

    return spec->read_string(v);
  }

  const std::string& provider::source() const {
    static const std::string s_builtin = "builtin";
    return s_builtin;
  }

  result<bool> provider::prepare() {
    auto res = describe();
    if (!res) {
      return res.err();
    }
    _spec = *res;
    return true;
  }
}
