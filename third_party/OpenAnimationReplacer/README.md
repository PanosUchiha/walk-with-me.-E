# Open Animation Replacer conditions SDK

Source: https://github.com/ersh1/OpenAnimationReplacer
Revision: `f4e7688b065175aff70aa523073857911e15aca3`

The five API source/header files were retrieved from the upstream `src/API`
directory. `COPYING` and `EXCEPTIONS` preserve its GPL-3.0 license and exceptions.
Only `OpenAnimationReplacerAPI-Conditions.cpp` is adapted: it uses the explicit
ANSI Windows module lookup in WWM's Unicode build, and API registration reads
WWM's explicit `Plugin::NAME` and `Plugin::VERSION` instead of depending on
CommonLib's automatic `PluginDeclaration` discovery. The custom condition
likewise supplies WWM's metadata directly. No OAR DLL is redistributed.

The conditions API is registered during SKSE PostLoad. WWM requests interface V3
(OAR 3.0+) and falls back to V2 for OAR 2.x, which refuses V3. Under V2 its
conditions report `kOld_Custom`, because OAR 2.x reads that vtable slot as
`GetConditionType()`. See `include/oar_conditions.h`.
