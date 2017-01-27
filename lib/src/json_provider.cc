#include <libral/json_provider.hpp>

#include <iostream>
#include <string>

#include <boost/algorithm/string.hpp>
#include <leatherman/execution/execution.hpp>

#include <leatherman/locale/locale.hpp>
#include <leatherman/logging/logging.hpp>

#include <libral/result.hpp>

#include <cstdio>

using namespace leatherman::locale;
namespace exe = leatherman::execution;
namespace json = leatherman::json_container;

namespace libral {

  result<changes>
  json_provider::json_resource::update(const attr_map &should) {
    auto inp = json::JsonContainer();
    inp.set<bool>({ "ral", "noop" }, false);
    inp.set<std::string>({ "resource", "name" }, name());

    for (auto k : should) {
      // FIXME: We should really map libral values <-> JSON values (e.g. bool)
      inp.set<std::string>({ "resource", k.first }, k.second.to_string());
    }
    auto out=_prov->run_action("update", inp);
    if (!out) {
      LOG_ERROR("provider[{1}]: {2}", _prov->_path, out.err().detail);
      return out.err();
    }

    std::string message, kind;
    if (_prov->contains_error(*out, message, kind)) {
      return error(_("update failed: {1}", message));
    }

    result<changes> res;
    auto& chgs = res.ok();

    // FIXME: should we consider this an error or an indication that no
    // changes were made ?
    if (! out->includes("changes")) {
      return chgs;
    }

    auto json_chgs = out->get<json::JsonContainer>("changes");
    for (auto k : json_chgs.keys()) {
      if (! json_chgs.includes({ k, "is" })) {
        return error(_("malformed change: entry for {1} does not contain 'is'",
                       k));
      }
      if (! json_chgs.includes({ k, "was" })) {
        return error(_("malformed change: entry for {1} does not contain 'was'",
                       k));
      }

      auto is = value(json_chgs.get<std::string>({ k, "is"}));
      auto was = value(json_chgs.get<std::string>({ k, "was"}));
      chgs.add(k, is, was);
    }
    for (auto s : should) {
      if (s.first != "name")
        operator[](s.first) = s.second;
    }
    return res;
  }

  result<prov::spec> json_provider::describe() {
    // Same as in simple_provider
    return prov::spec::read(_path, _node);
  }

  result<bool> json_provider::suitable() {
    // Same as in simple_provider
    auto meta = _node["provider"];
    if (! meta.IsMap()) {
      return error(_("expected 'provider' key in metadata to contain a map"));
    }
    auto s = meta["suitable"].as<std::string>();
    if (s != "true" && s != "false") {
      return error(_("provider {1} (simple): metadata 'suitable' must be either 'true' or 'false' but was '{2}'", _path, s));
    }
    return (s == "true");
  }

  void json_provider::flush() {
    // Noop. Not supported/needed
  }

  std::unique_ptr<resource> json_provider::create(const std::string& name) {
    // Same as simple_provider
    auto shared_this =
      std::static_pointer_cast<json_provider>(shared_from_this());
    return std::unique_ptr<resource>(new json_resource(shared_this, name));
  }

  boost::optional<std::unique_ptr<resource>>
  json_provider::find(const std::string &name) {
    auto inp = json::JsonContainer();
    inp.set<std::string>({ "resource", "name" }, name);

    auto out=run_action("find", inp);
    if (!out) {
      LOG_ERROR("provider[{1}]: {2}", _path, out.err().detail);
      return boost::none;
    }

    std::string message, kind;
    if (contains_error(*out, message, kind)) {
      if (kind == "unknown") {
        // expected outcome
      } else {
        LOG_WARNING("provider[{1}]: find for name '{2}' failed with error {3}",
                    _path, name, message);
      }
      return boost::none;
    }
    auto json_rsrc = out->get<json::JsonContainer>("resource");
    auto rsrc = resource_from_json(json_rsrc);
    if (!rsrc) {
      // FIXME: We need to return errors like this
      LOG_ERROR("provider[{1}]: find of '{2}': {3}",
                _path, name, rsrc.err().detail);
      return boost::none;
    }
    if ((*rsrc)->name() != name) {
      LOG_ERROR("provider[{1}]: find of name '{2}' returned resource named '{3}'", _path, name, (*rsrc)->name());
      return boost::none;
    }
    return std::move(*rsrc);
  }

  std::vector<std::unique_ptr<resource>> json_provider::instances() {
    // run script with ral_action == list
    std::vector<std::unique_ptr<resource>> result;
    auto inp = json::JsonContainer();
    auto out = run_action("list", inp);
    if (!out) {
      LOG_ERROR("provider[{1}]: {2}", _path, out.err().detail);
      return result;
    }
    std::string message, kind;
    if (contains_error(*out, message, kind)) {
      LOG_ERROR("provider[{1}]: list failed with error {2}",
                _path, message);
      // FIXME: We really need a way to return errors from instances()
      return result;
    }
    if (!out->includes("resources")) {
      LOG_ERROR("provider[{1}]: list did not produce a 'resources' entry",
                _path);
      // FIXME: We really need a way to return errors from instances()
      return result;
    }
    auto json_rsrcs = out->get<std::vector<json::JsonContainer>>("resources");
    for (auto json_rsrc : json_rsrcs) {
      auto rsrc = resource_from_json(json_rsrc);
      if (!rsrc) {
        LOG_ERROR("provider[{1}]: list failed: {2}", _path, rsrc.err().detail);
        return result;
      }
      result.push_back(std::move(*rsrc));
    }
    return result;
  }

  result<json::JsonContainer>
  json_provider::run_action(const std::string& action,
                            const json::JsonContainer& json) {
    auto inp = json.toString();
    auto res = exe::execute(_path, { "ral_action=" + action },
                            inp,
                            0, { exe::execution_options::trim_output,
                                exe::execution_options::merge_environment });
    if (!res.success) {
      if (res.output.empty()) {
        if (res.error.empty()) {
          return error(_("action '{1}' exited with status {2}",
                         action, res.exit_code));
        } else {
          return error(_("action '{1}' exited with status {2}. stderr was '{3}'",
                         action, res.exit_code, res.error));
        }
      } else {
        if (res.error.empty()) {
          return error(_("action '{1}' exited with status {2}. Output was '{3}'",
                         action, res.exit_code, res.output));
        } else {
          return error(_("action '{1}' exited with status {2}. Output was '{3}'. stderr was '{4}'",
                         action, res.exit_code, res.output, res.error));
        }
      }
    }
    if (! res.error.empty()) {
      return error(_("action '{1}' produced stderr '{2}'", action, res.error));
    }

    // FIXME: this will probably throw like crazy
    auto reslt = json::JsonContainer(res.output);
    return reslt;
  }

  bool json_provider::contains_error(const json::JsonContainer& json,
                                     std::string& message,
                                     std::string& kind) {
    auto res = json.includes("error");
    if (res) {
      message = json.getWithDefault<std::string>({"error", "message"}, "");
      kind = json.getWithDefault<std::string>({"error", "kind"}, "failed");
    }
    return res;
  }

  result<std::unique_ptr<resource>>
  json_provider::resource_from_json(const json::JsonContainer& json) {
    std::unique_ptr<resource> rsrc;
    if (!json.includes("name")) {
      return error(_("resource does not have a name"));
    }

    auto name = json.get<std::string>("name");
    rsrc = create(name);

    for (auto k : json.keys()) {
      if (k == "name") {
        continue;
      }
      (*rsrc)[k] = value(json.get<std::string>(k));
    }
    return std::move(rsrc);
  }
}
