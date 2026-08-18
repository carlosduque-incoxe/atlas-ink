#include "HttpDownloader.h"

#include <Arduino.h>
#include <HttpAuthPolicy.h>
#include <Logging.h>
#include <Memory.h>
#include <base64.h>

#include <functional>
#include <string>

#if defined(FREEINK_NET_WOLFSSL)
#include <SecureHttpClient.h>

extern "C" void wolfSSL_Arduino_Serial_Print(const char* const msg) { LOG_DBG("WOLFSSL", "%s", msg); }
#else
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#endif

namespace {
#if !defined(FREEINK_NET_WOLFSSL)
// RX holds the response headers. Smaller buffers leave enough contiguous heap
// for mbedTLS on redirect-heavy OPDS feeds while still preserving the headers
// we read directly (Location, Content-Length).
constexpr int HTTP_RX_BUF = 2048;
constexpr int HTTP_TX_BUF = 512;
#endif
// Per-socket timeout and redirect count come from RequestOptions. Defaults
// preserve the historic 60 s / five-hop behavior for existing callers.
constexpr size_t READ_CHUNK = 1024;

void secureClear(std::string& value) {
  volatile char* data = value.empty() ? nullptr : &value[0];
  for (size_t i = 0; i < value.size(); ++i) data[i] = 0;
  value.clear();
}

struct Sink {
  std::function<bool(const uint8_t*, size_t)> write;  // returns false to abort the transfer
  HttpDownloader::ProgressCallback progress;
  bool* cancelFlag = nullptr;
  const std::atomic<bool>* atomicCancelFlag = nullptr;
  size_t total = 0;
  size_t downloaded = 0;
  unsigned long startedAt = 0;
  uint32_t overallTimeoutMs = 0;
  bool timedOut = false;
};

bool isRedirect(int status) {
  return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

bool hasBasicAuth(const HttpDownloader::RequestOptions& options) {
  return http_auth_policy::hasBasicAuth(options.username);
}

bool hasAnyBasicCredential(const HttpDownloader::RequestOptions& options) {
  return !options.username.empty() || !options.password.empty();
}

bool hasBearerAuth(const HttpDownloader::RequestOptions& options) { return !options.bearerToken.empty(); }

bool validateAuthOptions(const HttpDownloader::RequestOptions& options) {
  if (hasAnyBasicCredential(options) && hasBearerAuth(options)) {
    LOG_ERR("HTTP", "Basic and Bearer credentials cannot be combined");
    return false;
  }
  return true;
}

bool shouldAbort(Sink& sink) {
  if ((sink.cancelFlag && *sink.cancelFlag) ||
      (sink.atomicCancelFlag && sink.atomicCancelFlag->load(std::memory_order_acquire))) {
    return true;
  }
  if (sink.overallTimeoutMs > 0 && millis() - sink.startedAt >= sink.overallTimeoutMs) {
    sink.timedOut = true;
    return true;
  }
  return false;
}

void clearCredentials(HttpDownloader::RequestOptions& options) {
  secureClear(options.username);
  secureClear(options.password);
  secureClear(options.bearerToken);
}

#if defined(FREEINK_NET_WOLFSSL)
HttpDownloader::DownloadError runGetWolf(const std::string& startUrl, const HttpDownloader::RequestOptions& options,
                                         Sink& sink) {
  std::string url = startUrl;
  HttpDownloader::RequestOptions activeOptions = options;

  for (int hop = 0; hop <= options.maxRedirects; ++hop) {
    if (shouldAbort(sink)) return sink.timedOut ? HttpDownloader::HTTP_ERROR : HttpDownloader::ABORTED;
    freeink::SecureHttpClient http;
    http.setTimeout(options.timeoutMs);
    http.setInsecure();
    if (!http.begin(url)) {
      LOG_ERR("HTTP", "wolfSSL bad URL: %s", url.c_str());
      return HttpDownloader::HTTP_ERROR;
    }
    // setUserAgent replaces SecureHttpClient's built-in UA; addHeader would
    // append a second User-Agent header, which strict servers reject (aiohttp
    // answers 400 "Duplicate 'User-Agent' header found").
    http.setUserAgent("CrossPoint-ESP32-" CROSSPOINT_VERSION);
    if (hasBasicAuth(activeOptions)) {
      std::string credentials = activeOptions.username + ":" + activeOptions.password;
      std::string encoded = base64::encode(credentials.c_str()).c_str();
      std::string header = std::string("Basic ") + encoded;
      http.addHeader("Authorization", header);
      secureClear(credentials);
      secureClear(encoded);
      secureClear(header);
    } else if (hasBearerAuth(activeOptions)) {
      std::string header = std::string("Bearer ") + activeOptions.bearerToken;
      http.addHeader("Authorization", header);
      secureClear(header);
    }

    LOG_DBG("HTTP", "wolfSSL GET: %s", url.c_str());
    const int status = http.GET(
        [&http, &sink](const uint8_t* data, size_t len) {
          if (http.getStatus() != 200) return true;
          if (sink.total == 0 && http.hasContentLength()) sink.total = http.getContentLength();
          if (!sink.write(data, len)) return false;
          sink.downloaded += len;
          if (sink.progress && sink.total > 0) sink.progress(sink.downloaded, sink.total);
          return true;
        },
        [&sink]() { return shouldAbort(sink); });

    if (http.aborted()) return sink.timedOut ? HttpDownloader::HTTP_ERROR : HttpDownloader::ABORTED;
    if (status < 0) {
      LOG_ERR("HTTP", "wolfSSL request failed: %s", url.c_str());
      return HttpDownloader::HTTP_ERROR;
    }
    if (isRedirect(status)) {
      if (hop >= options.maxRedirects) {
        LOG_ERR("HTTP", "too many redirects");
        return HttpDownloader::HTTP_ERROR;
      }
      const std::string location = http.getHeader("location");
      std::string nextUrl;
      if (location.empty() || !freeink::SecureHttpClient::resolveUrl(url, location, nextUrl)) {
        LOG_ERR("HTTP", "wolfSSL bad redirect: %d", status);
        return HttpDownloader::HTTP_ERROR;
      }
      if ((hasBasicAuth(activeOptions) || hasBearerAuth(activeOptions)) &&
          !http_auth_policy::sameOrigin(url, nextUrl)) {
        LOG_DBG("HTTP", "Stripping credentials on cross-origin redirect");
        clearCredentials(activeOptions);
      }
      url = std::move(nextUrl);
      continue;
    }
    if (status != 200) {
      LOG_ERR("HTTP", "wolfSSL unexpected status: %d", status);
      return HttpDownloader::HTTP_ERROR;
    }
    if (http.callbackAborted()) return HttpDownloader::FILE_ERROR;
    if (!http.responseComplete()) {
      LOG_ERR("HTTP", "wolfSSL incomplete: got %zu of %zu bytes", sink.downloaded, sink.total);
      return HttpDownloader::HTTP_ERROR;
    }
    return HttpDownloader::OK;
  }
  LOG_ERR("HTTP", "too many redirects");
  return HttpDownloader::HTTP_ERROR;
}
#endif

#if !defined(FREEINK_NET_WOLFSSL)
// Streams a GET body through sink.write in READ_CHUNK pieces. Uses the manual
// open/fetch_headers/read path rather than esp_http_client_perform(): perform()
// pushes the whole body through an event callback and reports a chunked body
// that ends early as ESP_ERR_HTTP_INCOMPLETE_DATA, whereas the read loop streams
// large/slow files and surfaces a short read directly.
HttpDownloader::DownloadError runGet(const std::string& url, const HttpDownloader::RequestOptions& options,
                                     Sink& sink) {
  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.buffer_size = HTTP_RX_BUF;
  config.buffer_size_tx = HTTP_TX_BUF;
  config.timeout_ms = options.timeoutMs;
  // Verify HTTPS against the bundled CA roots. This build has esp-tls
  // CONFIG_ESP_TLS_INSECURE off, so an unverified TLS handshake can't be set
  // up at all; the model is public servers over verified https and local
  // servers over plain http (esp_http_client picks the transport from the URL
  // scheme, so http:// needs no cert config). The prior setInsecure() worked
  // only because Arduino's ssl_client drives mbedtls directly.
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.keep_alive_enable = true;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    LOG_ERR("HTTP", "client init failed");
    return HttpDownloader::HTTP_ERROR;
  }

  esp_http_client_set_header(client, "User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);
  if (hasBasicAuth(options)) {
    // Preemptive Basic auth, like the prior addHeader; don't wait for a 401.
    std::string credentials = options.username + ":" + options.password;
    std::string header = ("Basic " + base64::encode(credentials.c_str())).c_str();
    esp_http_client_set_header(client, "Authorization", header.c_str());
    secureClear(credentials);
    secureClear(header);
  } else if (hasBearerAuth(options)) {
    std::string header = "Bearer " + options.bearerToken;
    esp_http_client_set_header(client, "Authorization", header.c_str());
    secureClear(header);
  }

  // open()/read() does not auto-follow redirects (only perform() does), so step
  // 30x responses manually. OPDS download endpoints and the GitHub release CDN
  // both redirect.
  if (shouldAbort(sink)) {
    esp_http_client_cleanup(client);
    return sink.timedOut ? HttpDownloader::HTTP_ERROR : HttpDownloader::ABORTED;
  }
  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    LOG_ERR("HTTP", "open failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return HttpDownloader::HTTP_ERROR;
  }
  int64_t contentLength = esp_http_client_fetch_headers(client);
  int status = esp_http_client_get_status_code(client);
  bool credentialsActive = hasBasicAuth(options) || hasBearerAuth(options);
  auto redirectUrl = makeUniqueNoThrow<char[]>(1024);
  for (int hop = 0; isRedirect(status) && hop < options.maxRedirects; ++hop) {
    if (shouldAbort(sink)) {
      esp_http_client_cleanup(client);
      return sink.timedOut ? HttpDownloader::HTTP_ERROR : HttpDownloader::ABORTED;
    }
    std::string previousUrl;
    if (redirectUrl && esp_http_client_get_url(client, redirectUrl.get(), 1024) == ESP_OK) {
      previousUrl = redirectUrl.get();
    }
    if (esp_http_client_set_redirection(client) != ESP_OK) break;
    if (credentialsActive) {
      bool sameRedirectOrigin = false;
      if (redirectUrl && !previousUrl.empty() && esp_http_client_get_url(client, redirectUrl.get(), 1024) == ESP_OK) {
        sameRedirectOrigin = http_auth_policy::sameOrigin(previousUrl, redirectUrl.get());
      }
      if (!sameRedirectOrigin) {
        LOG_DBG("HTTP", "Stripping credentials on cross-origin or unverifiable redirect");
        esp_http_client_delete_header(client, "Authorization");
        credentialsActive = false;
      }
    }
    esp_http_client_close(client);
    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
      LOG_ERR("HTTP", "redirect open failed: %s", esp_err_to_name(err));
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }
    contentLength = esp_http_client_fetch_headers(client);
    status = esp_http_client_get_status_code(client);
  }

  if (status != 200) {
    LOG_ERR("HTTP", "unexpected status: %d", status);
    esp_http_client_cleanup(client);
    return HttpDownloader::HTTP_ERROR;
  }

  // fetch_headers returns 0 for a chunked response (no Content-Length); leave
  // total at 0 so progress stays silent and the size check is skipped.
  sink.total = contentLength > 0 ? static_cast<size_t>(contentLength) : 0;

  auto buf = makeUniqueNoThrow<char[]>(READ_CHUNK);
  if (!buf) {
    LOG_ERR("HTTP", "OOM: %u byte read buffer", (unsigned)READ_CHUNK);
    esp_http_client_cleanup(client);
    return HttpDownloader::HTTP_ERROR;
  }

  while (true) {
    if (shouldAbort(sink)) {
      esp_http_client_cleanup(client);
      return sink.timedOut ? HttpDownloader::HTTP_ERROR : HttpDownloader::ABORTED;
    }
    const int read = esp_http_client_read(client, buf.get(), READ_CHUNK);
    if (read < 0) {
      LOG_ERR("HTTP", "read error after %zu bytes", sink.downloaded);
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }
    if (read == 0) break;  // all data received
    if (!sink.write(reinterpret_cast<const uint8_t*>(buf.get()), read)) {
      esp_http_client_cleanup(client);
      return HttpDownloader::FILE_ERROR;
    }
    sink.downloaded += read;
    if (sink.progress && sink.total > 0) sink.progress(sink.downloaded, sink.total);
  }

  const bool complete = esp_http_client_is_complete_data_received(client);
  esp_http_client_cleanup(client);
  if (!complete) {
    LOG_ERR("HTTP", "incomplete: got %zu of %zu bytes", sink.downloaded, sink.total);
    return HttpDownloader::HTTP_ERROR;
  }
  return HttpDownloader::OK;
}
#endif  // !FREEINK_NET_WOLFSSL

// All HTTP(S) fetches go through wolfSSL when it is the active TLS stack: it
// speaks TLS 1.3 and reads large bodies from servers where the esp_http_client/
// mbedTLS path fails to connect or stalls mid-stream. Plain-http URLs still use a
// WiFiClient inside runGetWolf, so this is safe for non-TLS targets too.
HttpDownloader::DownloadError runGetSecure(const std::string& url, const HttpDownloader::RequestOptions& options,
                                           Sink& sink) {
  if (!validateAuthOptions(options)) return HttpDownloader::HTTP_ERROR;
  sink.startedAt = millis();
  sink.overallTimeoutMs = options.overallTimeoutMs;
  sink.timedOut = false;
#if defined(FREEINK_NET_WOLFSSL)
  return runGetWolf(url, options, sink);
#else
  return runGet(url, options, sink);
#endif
}
}  // namespace

HttpDownloader::RequestOptions::~RequestOptions() { clearCredentials(*this); }

bool HttpDownloader::fetchUrl(const std::string& url, Stream& outContent, const std::string& username,
                              const std::string& password) {
  LOG_DBG("HTTP", "Fetching: %s", url.c_str());
  RequestOptions options;
  options.username = username;
  options.password = password;
  Sink sink;
  sink.write = [&outContent](const uint8_t* data, size_t len) { return outContent.write(data, len) == len; };
  return runGetSecure(url, options, sink) == OK;
}

bool HttpDownloader::fetchUrl(const std::string& url, std::string& outContent, const std::string& username,
                              const std::string& password) {
  LOG_DBG("HTTP", "Fetching: %s", url.c_str());
  RequestOptions options;
  options.username = username;
  options.password = password;
  outContent.clear();  // start clean; the sink appends, so don't carry prior content
  Sink sink;
  sink.write = [&outContent](const uint8_t* data, size_t len) {
    outContent.append(reinterpret_cast<const char*>(data), len);
    return true;
  };
  return runGetSecure(url, options, sink) == OK;
}

bool HttpDownloader::fetchUrl(const std::string& url, const DataCallback& onData, const std::string& username,
                              const std::string& password) {
  LOG_DBG("HTTP", "Fetching: %s", url.c_str());
  RequestOptions options;
  options.username = username;
  options.password = password;
  return fetchUrl(url, onData, options);
}

bool HttpDownloader::fetchUrl(const std::string& url, const DataCallback& onData, const RequestOptions& options) {
  LOG_DBG("HTTP", "Fetching: %s", url.c_str());
  Sink sink;
  sink.atomicCancelFlag = options.cancelFlag;
  sink.write = onData;
  return runGetSecure(url, options, sink) == OK;
}

HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string& url, const std::string& destPath,
                                                             ProgressCallback progress, bool* cancelFlag,
                                                             const std::string& username, const std::string& password) {
  LOG_DBG("HTTP", "Downloading: %s -> %s", url.c_str(), destPath.c_str());

  if (Storage.exists(destPath.c_str())) {
    Storage.remove(destPath.c_str());
  }
  HalFile file;
  if (!Storage.openFileForWrite("HTTP", destPath.c_str(), file)) {
    LOG_ERR("HTTP", "Failed to open file for writing");
    return FILE_ERROR;
  }

  Sink sink;
  sink.progress = std::move(progress);
  sink.cancelFlag = cancelFlag;
  sink.write = [&file](const uint8_t* data, size_t len) { return file.write(data, len) == len; };

  RequestOptions options;
  options.username = username;
  options.password = password;
  const DownloadError result = runGetSecure(url, options, sink);
  // Close before any remove() on the same path; DESTRUCTOR_CLOSES_FILE would
  // otherwise close only after the remove.
  file.close();

  if (result != OK) {
    Storage.remove(destPath.c_str());
    return result;
  }
  if (sink.downloaded == 0) {
    LOG_ERR("HTTP", "no data received");
    Storage.remove(destPath.c_str());
    return HTTP_ERROR;
  }
  LOG_DBG("HTTP", "Downloaded %zu bytes", sink.downloaded);
  return OK;
}
