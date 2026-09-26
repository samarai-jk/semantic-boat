#pragma once

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>
#include "slstm32/driver.hpp"
#include "slstm32/service.hpp"

namespace slstm32 {

template <typename Tag, typename Component>
struct ComponentBinding {
    using tag_type = Tag;
    using component_type = Component;
    Component* component{};
};

template <typename Tag, typename Component>
constexpr auto bind(Component& component) {
    return ComponentBinding<Tag, Component>{&component};
}

template <typename Base, typename... Bindings>
class ComponentSet {
public:
    static_assert((std::is_base_of_v<Base, typename Bindings::component_type> && ...),
                  "Every registered component must derive from the requested base class");

    explicit constexpr ComponentSet(Bindings... bindings) : bindings_(bindings...) {}

    bool initAll() {
        bool ok = true;
        std::apply([&ok](auto&... binding) {
            ((ok = binding.component->init() && ok), ...);
        }, bindings_);
        return ok;
    }

    void runAll() {
        std::apply([](auto&... binding) {
            const auto runOne = [](auto& item) {
                if (item.component->enabled()) item.component->run();
            };
            (runOne(binding), ...);
        }, bindings_);
    }

    template <typename Tag>
    decltype(auto) get() {
        static_assert(tagCount<Tag>() == 1u, "A component tag must be registered exactly once");
        return getImpl<Tag, 0u>();
    }

    template <typename Tag>
    decltype(auto) get() const {
        static_assert(tagCount<Tag>() == 1u, "A component tag must be registered exactly once");
        return getImpl<Tag, 0u>();
    }

    static constexpr std::size_t size() { return sizeof...(Bindings); }

private:
    template <typename Tag>
    static constexpr std::size_t tagCount() {
        return (std::size_t{} + ... +
                (std::is_same_v<Tag, typename Bindings::tag_type> ? 1u : 0u));
    }

    template <typename Tag, std::size_t Index>
    decltype(auto) getImpl() {
        using Binding = std::tuple_element_t<Index, std::tuple<Bindings...>>;
        if constexpr (std::is_same_v<Tag, typename Binding::tag_type>) {
            return *std::get<Index>(bindings_).component;
        } else {
            return getImpl<Tag, Index + 1u>();
        }
    }

    template <typename Tag, std::size_t Index>
    decltype(auto) getImpl() const {
        using Binding = std::tuple_element_t<Index, std::tuple<Bindings...>>;
        if constexpr (std::is_same_v<Tag, typename Binding::tag_type>) {
            return std::as_const(*std::get<Index>(bindings_).component);
        } else {
            return getImpl<Tag, Index + 1u>();
        }
    }

    std::tuple<Bindings...> bindings_;
};

template <typename... Bindings>
constexpr auto makeDriverSet(Bindings... bindings) {
    return ComponentSet<Driver, Bindings...>{bindings...};
}

template <typename... Bindings>
constexpr auto makeServiceSet(Bindings... bindings) {
    return ComponentSet<Service, Bindings...>{bindings...};
}

template <typename DriverSet, typename ServiceSet>
class Application {
public:
    constexpr Application(DriverSet drivers, ServiceSet services)
        : drivers_(std::move(drivers)), services_(std::move(services)) {}

    bool init() {
        if (!drivers_.initAll()) return false;
        return services_.initAll();
    }

    void run() {
        drivers_.runAll();
        services_.runAll();
    }

    DriverSet& drivers() { return drivers_; }
    const DriverSet& drivers() const { return drivers_; }
    ServiceSet& services() { return services_; }
    const ServiceSet& services() const { return services_; }

private:
    DriverSet drivers_;
    ServiceSet services_;
};

template <typename DriverSet, typename ServiceSet>
constexpr auto makeApplication(DriverSet drivers, ServiceSet services) {
    return Application<DriverSet, ServiceSet>{std::move(drivers), std::move(services)};
}

} // namespace slstm32
