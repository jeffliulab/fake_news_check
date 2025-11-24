# CS112 LLM-Enhanced Web Proxy

A web proxy with integrated AI-powered content analysis using Tufts University's LLM service.

## About

This project extends a traditional HTTP/HTTPS proxy with Large Language Model (LLM) capabilities to provide intelligent web content analysis. It uses **Tufts University's LLM API service** to analyze webpage content in real-time, offering features like content summarization and fake news detection.

## Architecture Overview

```
    ┌──────────┐                                              ┌─────────────┐
    │          │          1. Request                          │             │
    │ Browser  │ ────────────────────────────────────────────►│   Target    │
    │          │                                              │   Website   │
    └─────┬────┘                                              └──────┬──────┘
          │                                                          │
          │                                                          │ 2. Response
          │                                                          │   (HTML)
          │                                                          ▼
          │                  ┌───────────────┐            ┌─────────────────┐
          │                  │               │◄───────────┤  C Proxy        │
          │   6. Enhanced    │  C Proxy      │ 3. Intercept   (Port 8080)  │
          │      HTML        │  (Port 8080)  │    HTML     │                │
          │◄─────────────────┤               │             └─────────────────┘
          │                  └───────┬───────┘
          │                          │
          │                          │ 4. Send HTML
          │                          │    for Analysis
          │                          ▼
          │                  ┌───────────────┐
          │                  │ Flask Service │
          │                  │  (Port 5000)  │
          │                  │               │
          │                  │ • Inject JS   │
          │                  │ • Enhance HTML│
          │                  └───────┬───────┘
          │                          │
          │                          │ 5. Call LLM API
          │                          │    (Async Analysis)
          │                          ▼
          │                 ┌─────────────────┐
          │                 │  Tufts LLM API  │
          │                 │                 │
          │                 │ • Summarization │
          │                 │ • Fake News AI  │
          │                 └─────────────────┘
```

## Key Features

- **Dual Mode Operation**:
  - **Simple Mode**: Standard proxy with header injection
  - **LLM Mode**: AI-enhanced content analysis with Tufts LLM
  
- **Real-time AI Analysis**:
  - Automatic content summarization
  - Fake news detection
  - Asynchronous processing (page loads immediately)
  
- **Flexible Architecture**:
  - C-based high-performance proxy server
  - Python Flask middleware for LLM integration
  - Support for Python and C client libraries

## Quick Start

See [QUICKSTART.md](QUICKSTART.md) for detailed setup and testing instructions.

### Basic Usage

1. **Simple Mode** (standard proxy):
   ```bash
   ./proxy 8080 proxy_ca.crt proxy_ca.key
   ```

2. **LLM Mode** (AI-enhanced):
   ```bash
   # Start Flask service first
   python3 fake_news.py
   
   # Then start proxy with LLM enabled
   ./proxy 8080 proxy_ca.crt proxy_ca.key llm=true
   ```

3. **Configure your browser** to use proxy at `127.0.0.1:8080`

## Project Structure

```
.
├── c/               # C proxy implementation
├── py/              # Python client libraries and Flask service
├── fake_news.py     # Flask middleware with Tufts LLM integration
├── proxy_ca.crt     # CA certificate for HTTPS interception
├── proxy_ca.key     # CA private key
├── README.md        # This file
└── QUICKSTART.md    # Detailed setup and testing guide
```

## Technology Stack

- **Proxy Server**: C (high performance, low latency)
- **LLM Integration**: Python + Flask
- **AI Service**: Tufts University LLM API
- **TLS/SSL**: OpenSSL for HTTPS interception
- **Client Libraries**: Python and C

## Documentation

- [QUICKSTART.md](QUICKSTART.md) - Setup, usage, and testing guide
- [py/README.md](py/README.md) - Python client library documentation
- [c/README.md](c/README.md) - C client library documentation

## Use Cases

- **Educational**: Learn about web proxies, TLS interception, and LLM integration
- **Content Analysis**: Automatic webpage summarization and analysis
- **Security Research**: Fake news detection and content verification
- **Development**: Test and debug web applications with AI insights

## License

CS112 Course Project - Tufts University

---

**Note**: This project uses Tufts University's LLM API service for AI-powered content analysis. An API key is required for LLM mode functionality.
