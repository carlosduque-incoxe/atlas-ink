#pragma once

namespace atlas_release_key {

// ECDSA P-256 release key. The private half stays outside GitHub and the
// repository; Atlas signs verified CI artifacts locally before publication.
inline constexpr char PEM[] = R"KEY(-----BEGIN PUBLIC KEY-----
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEBSL3ItPtoKy8ja4/BAq64pKNQLno
3bysyMDZvs6x/HgkmDfRqz9I+LSQvgRpqBdPTkaDaWGi3F2mZsl0tXQWbw==
-----END PUBLIC KEY-----
)KEY";

}  // namespace atlas_release_key