#pragma once
#include <windows.h>
#include <map>
#include <string>
// Verbatim v32 scope methods/fields, extracted before the fix; offline regression only.
class LegacyScope {
public:
    static UINT GetOwner()
    {
        _lastOwner++;
        return _lastOwner;
    }

    // Moved checks here to prevent circular includes
    /// <summary>
    /// Enables skipping of LoadLibrary checks
    /// </summary>
    /// <param name="dllName">Lower case dll name without `.dll` at the end. Leave blank for skipping all dll's</param>
    static void DisableChecks(UINT owner, std::string dllName = "")
    {
        // if (_skipOwner == 0 || _skipOwner < owner)
        //{
        _skipOwner = owner;
        _skipChecks = true;
        _skipDllName[_skipOwner] = dllName;
        //}
    };

    static void EnableChecks(UINT owner)
    {
        _skipDllName.erase(_skipOwner);

        // if (_skipOwner == owner)
        //{
        if (_skipDllName.size() > 0)
        {
            // loop in reverse to get the last added owner
            _skipOwner = _skipDllName.rbegin()->first;
        }
        else
        {
            _skipOwner = 0;
        }

        _skipChecks = (_skipOwner != 0);
        //}
    };

    static void DisableServeOriginal(UINT owner)
    {
        if (_serveOwner == 0 || _serveOwner == owner)
        {
            _serveOriginal = false;
            _skipOwner = 0;
        }
    };

    static void EnableServeOriginal(UINT owner)
    {
        if (_serveOwner == 0 || _serveOwner == owner)
        {
            _serveOriginal = true;
            _skipOwner = owner;
        }
    };

    static bool SkipDllChecks() { return _skipChecks; }
    static std::string SkipDllName()
    {
        return _skipOwner == 0 ? "" : (_skipDllName.contains(_skipOwner) ? _skipDllName[_skipOwner] : "");
    }
    static bool ServeOriginal() { return _serveOriginal; }

  private:
    inline static bool _skipChecks = false;
    inline static std::map<UINT, std::string> _skipDllName;
    inline static UINT _skipOwner = 0;
    inline static UINT _lastOwner = 0;

    inline static bool _serveOriginal = false;
    inline static UINT _serveOwner = 0;

};
