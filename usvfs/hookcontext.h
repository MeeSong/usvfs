/*
Userspace Virtual Filesystem

Copyright (C) 2015 Sebastian Herbord. All rights reserved.

This file is part of usvfs.

usvfs is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

usvfs is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with usvfs. If not, see <http://www.gnu.org/licenses/>.
*/
#pragma once

#include "redirectiontree.h"
#include "dllimport.h"
#include "semaphore.h"
#include <usvfsparameters.h>
#include <directory_tree.h>
#include <exceptionex.h>
#include <winapi.h>
#include <boost/any.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <boost/thread/shared_lock_guard.hpp>
#include <memory>
#include <set>
#include <future>

namespace usvfs
{

#include <windows.h>
#undef min
#undef max


/**
 * @brief context available to hooks. This is protected by a many-reader
 * single-writer mutex
 */
class HookContext
{

public:
  typedef std::unique_ptr<const HookContext, void (*)(const HookContext *)>
      ConstPtr;
  typedef std::unique_ptr<HookContext, void (*)(HookContext *)> Ptr;
  typedef unsigned int DataIDT;

public:
  HookContext(const Parameters &params, HMODULE module);

  DLLEXPORT ~HookContext();

  /**
   * @brief get read access to the context.
   * @return smart ptr to the context. mutex will automatically be released when
   * this leaves scope
   */
  static ConstPtr readAccess(const char *source);

  /**
   * @brief get write access to the context.
   * @return smart ptr to the context. mutex will automatically be released when
   * this leaves scope
   */
  static Ptr writeAccess(const char *source);

  /**
   * @return table containing file redirection information
   */
  RedirectionTreeContainer &redirectionTable()
  {
    return m_Tree;
  }

  /**
   * @return table containing file redirection information
   */
  const RedirectionTreeContainer &redirectionTable() const
  {
    return m_Tree;
  }

  /**
   * @return the parameters passed in on dll initialisation
   */
  Parameters callParameters() const;

  /**
   * @return true if usvfs is running in debug mode
   */
  bool debugMode() const
  {
    return m_DebugMode;
  }

  /**
   * @return path to the calling library itself
   */
  std::wstring dllPath() const;

  /**
   * @brief get access to custom data
   * @note the caller gains write access to the data, independent on the lock on
   * the context
   *       as a whole. The caller himself has to ensure thread safety
   */
  template <typename T> T &customData(DataIDT id) const
  {
    auto iter = m_CustomData.find(id);
    if (iter == m_CustomData.end()) {
      iter = m_CustomData.insert(std::make_pair(id, T())).first;
    }
    // std::map is supposed to not invalidate any iterators when elements are
    // added
    // so it should be safe to return a pointer here
    T *res = boost::any_cast<T>(&iter->second);
    return *res;
  }

  void registerProcess(DWORD pid);
  void unregisterCurrentProcess();
  std::vector<DWORD> registeredProcesses() const;

  void updateParameters() const;

  void registerDelayed(std::future<int> delayed);

  std::vector<std::future<int>> &delayed();

private:
  static void unlock(HookContext *instance);
  static void unlockShared(const HookContext *instance);

  SharedParameters *retrieveParameters(const Parameters &params);

private:
  static HookContext *s_Instance;

  shared::SharedMemoryT m_ConfigurationSHM;
#pragma message("this should be protected by a system-wide named mutex")
  SharedParameters *m_Parameters{nullptr};
  RedirectionTreeContainer m_Tree;

  std::vector<std::future<int>> m_Futures;

  mutable std::map<DataIDT, boost::any> m_CustomData;

  bool m_DebugMode{false};

  HMODULE m_DLLModule;

  //  mutable std::recursive_mutex m_Mutex;
  mutable RecursiveBenaphore m_Mutex;
};
}

#if defined(UNITTEST) || defined(_WINDLL)
// exposed only to unit tests for easier testability
extern "C" DLLEXPORT usvfs::HookContext *__cdecl CreateHookContext(
    const usvfs::Parameters &params, HMODULE module);
#endif

// declare an identifier that is guaranteed to be unique across the application
#define DATA_ID(name)                                                          \
  static const usvfs::HookContext::DataIDT name = __COUNTER__

// set of macros. These ensure a call context is created but most of all these
// ensure exceptions are caught.

#define READ_CONTEXT() HookContext::readAccess(__MYFUNC__)
#define WRITE_CONTEXT() HookContext::writeAccess(__MYFUNC__)

#define HOOK_START_GROUP(group)                                                \
  try {                                                                        \
    HookCallContext callContext(group);

#define HOOK_START                                                             \
  try {                                                                        \
    HookCallContext callContext;

#define HOOK_END                                                               \
  }                                                                            \
  catch (const std::exception &e)                                              \
  {                                                                            \
    spdlog::get("usvfs")                                                       \
        ->error("exception in {0}: {1}", __MYFUNC__, e.what());                \
    logExtInfo(e);                                                             \
  }

#define HOOK_ENDP(param)                                                       \
  }                                                                            \
  catch (const std::exception &e)                                              \
  {                                                                            \
    spdlog::get("usvfs")                                                       \
        ->error("exception in {0} ({1}): {2}", __MYFUNC__, param, e.what());   \
    logExtInfo(e);                                                             \
  }

#define PRE_REALCALL
#define POST_REALCALL callContext.updateLastError();
