#include "privatefile.h"
#include <QFile>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <aclapi.h>
#include <io.h>
#include <memory>
#include <vector>
#endif

namespace shadowlogis {
#ifdef Q_OS_WIN
namespace {
struct CloseHandleDeleter { void operator()(void *value) const { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); } };
struct LocalFreeDeleter { void operator()(void *value) const { if (value) LocalFree(value); } };
using Handle = std::unique_ptr<void, CloseHandleDeleter>;
using Local = std::unique_ptr<void, LocalFreeDeleter>;
std::vector<unsigned char> userToken() {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
        throw std::runtime_error("Cannot identify the Windows user for private file access.");
    Handle token(raw);
    DWORD size = 0;
    GetTokenInformation(raw, TokenUser, nullptr, 0, &size);
    if (!size) throw std::runtime_error("Cannot read the Windows user token.");
    std::vector<unsigned char> bytes(size);
    if (!GetTokenInformation(raw, TokenUser, bytes.data(), size, &size))
        throw std::runtime_error("Cannot read the Windows user token.");
    return bytes;
}
bool privateDescriptor(PSECURITY_DESCRIPTOR descriptor, PACL acl, PSID user) {
    SECURITY_DESCRIPTOR_CONTROL control = 0; DWORD revision = 0;
    if (!acl || acl->AceCount != 1 || !GetSecurityDescriptorControl(descriptor, &control, &revision) ||
            !(control & SE_DACL_PROTECTED)) return false;
    void *raw = nullptr;
    if (!GetAce(acl, 0, &raw)) return false;
    const auto *ace = static_cast<ACCESS_ALLOWED_ACE *>(raw);
    return ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE && !ace->Header.AceFlags &&
           (ace->Mask & FILE_ALL_ACCESS) == FILE_ALL_ACCESS &&
           EqualSid(const_cast<DWORD *>(&ace->SidStart), user);
}
}
#endif
void makeFilePrivate(QFileDevice &file) {
    if (!file.isOpen()) throw std::runtime_error("Cannot protect a closed output file.");
#ifdef Q_OS_WIN
    const int descriptor = file.handle();
    if (descriptor < 0) throw std::runtime_error("Output file has no native handle.");
    const auto original = reinterpret_cast<HANDLE>(_get_osfhandle(descriptor));
    // QFile's normal write handle lacks WRITE_DAC. Reopen the same object to
    // avoid applying permissions to the destination instead of QSaveFile's temp.
    Handle handle(ReOpenFile(original, READ_CONTROL | WRITE_DAC,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0));
    if (handle.get() == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot protect this Windows output. Choose a private NTFS folder.");
    auto token = userToken();
    auto *user = reinterpret_cast<TOKEN_USER *>(token.data())->User.Sid;
    EXPLICIT_ACCESSW access{};
    access.grfAccessPermissions = FILE_ALL_ACCESS;
    access.grfAccessMode = SET_ACCESS;
    access.grfInheritance = NO_INHERITANCE;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.TrusteeType = TRUSTEE_IS_USER;
    access.Trustee.ptstrName = static_cast<LPWSTR>(user);
    PACL acl = nullptr;
    if (SetEntriesInAclW(1, &access, nullptr, &acl) != ERROR_SUCCESS)
        throw std::runtime_error("Cannot create private Windows file permissions.");
    Local aclMemory(acl);
    if (SetSecurityInfo(handle.get(), SE_FILE_OBJECT,
                        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                        nullptr, nullptr, acl, nullptr) != ERROR_SUCCESS)
        throw std::runtime_error("Cannot apply private Windows file permissions.");
    PSECURITY_DESCRIPTOR security = nullptr; PACL applied = nullptr;
    const auto result = GetSecurityInfo(handle.get(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                        nullptr, nullptr, &applied, nullptr, &security);
    Local securityMemory(security);
    if (result != ERROR_SUCCESS || !privateDescriptor(security, applied, user))
        throw std::runtime_error("This filesystem cannot enforce private file permissions. Choose an NTFS folder.");
#else
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        throw std::runtime_error("Cannot protect output file permissions.");
#endif
}
bool hasPrivateFilePermissions(const QString &path) {
#ifdef Q_OS_WIN
    auto token = userToken();
    PSECURITY_DESCRIPTOR security = nullptr; PACL acl = nullptr;
    auto name = path.toStdWString();
    const auto result = GetNamedSecurityInfoW(name.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                            nullptr, nullptr, &acl, nullptr, &security);
    Local securityMemory(security);
    return result == ERROR_SUCCESS && privateDescriptor(security, acl, reinterpret_cast<TOKEN_USER *>(token.data())->User.Sid);
#else
    return QFile::exists(path) && !(QFile::permissions(path) &
        (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
         QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther));
#endif
}
}
