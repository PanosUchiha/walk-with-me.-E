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

The conditions API is registered during SKSE PostLoad. WWM uses condition API
version 4 (introduced in OAR 3.1.0); the installed OAR 3.2.0 supports it.
