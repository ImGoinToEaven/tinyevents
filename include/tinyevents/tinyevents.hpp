#pragma once

#include <functional>
#include <list>
#include <set>
#include <map>
#include <vector>
#include <typeindex>
#include <utility>
#include <algorithm>
#include <cstdint>

namespace tinyevents
{
    class Token;

    class Dispatcher {
    public:
        Dispatcher() = default;
        Dispatcher(Dispatcher &&) noexcept = default;
        Dispatcher(const Dispatcher &) = delete;
        Dispatcher &operator=(Dispatcher &&) noexcept = default;

        template<typename T>
        std::uint64_t listen(const std::function<void(const T &)> &listener, int priority = 0) {
            return addListener<T>(listener, priority);
        }

        template<typename T, typename C>
        std::uint64_t listen(void (C::*memberFunc)(const T &), C *instance, int priority = 0) {
            return addListener<T>(
                [instance, memberFunc](const T &msg) {
                    std::invoke(memberFunc, instance, msg);
                },
                priority
            );
        }

        template<typename T>
        std::uint64_t listenOnce(const std::function<void(const T &)> &listener, int priority = 0) {
            const auto listenerId = nextListenerId;
            return listen<T>([this, listenerId, listener](const T &msg) {
                listenersScheduledForRemoval.insert(listenerId);
                listener(msg);
                listenersScheduledForRemoval.erase(listenerId);
                this->remove(listenerId);
            }, priority);
        }

        template<typename T, typename C>
        std::uint64_t listenOnce(void (C::*memberFunc)(const T &), C *instance, int priority = 0) {
            const auto listenerId = nextListenerId;
            return listen<T>(
                [this, listenerId, instance, memberFunc](const T &msg) {
                    listenersScheduledForRemoval.insert(listenerId);
                    std::invoke(memberFunc, instance, msg);
                    listenersScheduledForRemoval.erase(listenerId);
                    this->remove(listenerId);
                },
                priority
            );
        }

        template<typename T>
        void dispatch(const T &msg) {
            auto it = listenersByType.find(std::type_index(typeid(T)));
            if (it == listenersByType.end()) return;
            auto listenersCopy = it->second;
            for (const auto &entry : listenersCopy) {
                if (listenersScheduledForRemoval.find(entry.handle) == listenersScheduledForRemoval.end()) {
                    entry.callback(&msg);
                }
            }
        }

        template<typename T>
        void queue(const T &msg) {
            queuedDispatches.push_back([msg](Dispatcher &dispatcher) {
                dispatcher.dispatch(msg);
            });
        }

        void process() {
            for (auto &queuedDispatch : queuedDispatches) {
                queuedDispatch(*this);
            }
            queuedDispatches.clear();
        }

        void remove(const std::uint64_t handle) {
            if (listenersScheduledForRemoval.find(handle) != listenersScheduledForRemoval.end()) return;
            for (auto &listenersPair : listenersByType) {
                auto &vec = listenersPair.second;
                vec.erase(std::remove_if(vec.begin(), vec.end(), [handle](const ListenerEntry &entry) {
                    return entry.handle == handle;
                }), vec.end());
            }
        }

        [[nodiscard]] bool hasListener(std::uint64_t handle) const {
            if (listenersScheduledForRemoval.find(handle) != listenersScheduledForRemoval.end()) return false;
            for (const auto &listenersPair : listenersByType) {
                const auto &vec = listenersPair.second;
                if (std::any_of(vec.begin(), vec.end(), [handle](const ListenerEntry &entry) {
                        return entry.handle == handle;
                    }))
                    return true;
            }
            return false;
        }

    private:
        struct ListenerEntry {
            std::uint64_t handle;
            int priority;
            std::function<void(const void*)> callback;
        };

        template<typename T>
        std::uint64_t addListener(const std::function<void(const T &)> &listener, int priority) {
            auto &listeners = listenersByType[std::type_index(typeid(T))];
            const auto listenerHandle = nextListenerId++;
            ListenerEntry entry;
            entry.handle = listenerHandle;
            entry.priority = priority;
            entry.callback = [listener](const void *msg) {
                listener(*static_cast<const T *>(msg));
            };
            auto it = std::find_if(listeners.begin(), listeners.end(), [priority](const ListenerEntry &e) {
                return e.priority < priority;
            });
            listeners.insert(it, entry);
            return listenerHandle;
        }

        std::map<std::type_index, std::vector<ListenerEntry>> listenersByType;
        std::list<std::function<void(Dispatcher&)>> queuedDispatches;
        std::set<std::uint64_t> listenersScheduledForRemoval;
        std::uint64_t nextListenerId = 0;
    };

    class Token {
    public:
        Token(Dispatcher &dispatcher, const std::uint64_t handle)
            : dispatcher(dispatcher), _handle(handle), holdsResource(true) {}
        ~Token() {
            if (holdsResource) {
                dispatcher.get().remove(_handle);
            }
        }
        Token(const Token &) = delete;
        Token &operator=(const Token &) = delete;
        Token(Token &&other) noexcept
            : dispatcher(other.dispatcher), _handle(other._handle), holdsResource(other.holdsResource) {
            other.holdsResource = false;
        }
        Token &operator=(Token &&other) noexcept {
            if (this != &other) {
                if (holdsResource) {
                    dispatcher.get().remove(_handle);
                }
                dispatcher = other.dispatcher;
                _handle = other._handle;
                holdsResource = other.holdsResource;
                other.holdsResource = false;
            }
            return *this;
        }
        [[nodiscard]] std::uint64_t handle() const {
            return _handle;
        }
        void remove() {
            dispatcher.get().remove(_handle);
            holdsResource = false;
        }
    private:
        std::reference_wrapper<Dispatcher> dispatcher;
        std::uint64_t _handle;
        bool holdsResource;
    };
}
