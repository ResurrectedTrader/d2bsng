#pragma once

#include <string_view>

#include "Args.h"
#include "Instance.h"

namespace d2bs::script {

// The untyped declaration surface: everything a class needs, and nothing that
// names an engine. A typed view over it - one that knows what the native is -
// is the runtime's business, not an engine's.
class ClassDecl {
   public:
    explicit ClassDecl(void* state) : state_(state) {}

    [[nodiscard]] ClassKey Key() const;

    // A constructor reads its arguments and hands back the instance it adopted.
    // It is the only binding that can adopt, which is what Construction says.
    // Without one, the class cannot be constructed from script.
    ClassDecl& Constructor(Ctor fn, Construct behaviour = Construct::RequireNew);

    ClassDecl& Method(std::string_view name, Native fn);
    ClassDecl& Get(std::string_view name, Getter getter);
    // A setter reads the incoming value as argument 0; its return value is
    // ignored, as it is in the language.
    ClassDecl& GetSet(std::string_view name, Getter getter, Setter setter);

    ClassDecl& StaticMethod(std::string_view name, Native fn);

   private:
    void* state_;
};

// A named object with properties but no class - what `me` is. Its getters read
// live state rather than anything stored on the object, so it needs none of the
// machinery a class does.
class ObjectDecl {
   public:
    explicit ObjectDecl(void* state) : state_(state) {}

    // The object is an instance of this class, and what is declared here sits
    // on top of the class's own members. `make` produces the native it wraps,
    // once per script - which is what lets `me` be a Unit that also knows
    // about the session around it. The object owns what `make` returns, and
    // frees it through the class's own Destroy.
    ObjectDecl& Instance(ClassKey of, void* (*make)());

    ObjectDecl& Get(std::string_view name, Getter getter);
    ObjectDecl& GetSet(std::string_view name, Getter getter, Setter setter);
    ObjectDecl& Constant(std::string_view name, double value);

   private:
    void* state_;
};

}  // namespace d2bs::script
