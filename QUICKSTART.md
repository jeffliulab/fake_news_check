# Quick Start Guide

## LLMProxy Client Libraries

This repository contains client libraries for interacting with an LLMProxy backend server.

The API supports five operations:

- **generate** --- send a prompt and get model-generated output
- **retrieve** --- query previously uploaded or stored content
- **upload_text** --- upload raw text for storage, retrieval, and RAG style workflows
- **upload_file** --- upload PDFs for storage, retrieval, and RAG style workflows
- **model_info** --- to get the list of models available for your subscription plan

These operations are available through **Python** and **C** client libraries included in this repo.

------------------------------------------------------------------------

## Repository Structure

    .
    ├── c/          # C client (library + header + examples)
    ├── py/         # Python client (package + examples)
    └── README.md   # Main documentation

If you plan to use:

- **Python**, start in [`py/README.md`](py/README.md)
- **C**, start in [`c/README.md`](c/README.md)

------------------------------------------------------------------------

## Authentication & Configuration

Both clients require a `.env` file providing:

    LLMPROXY_API_KEY="your-api-key-here"
    LLMPROXY_ENDPOINT="https://a061igc186.execute-api.us-east-1.amazonaws.com/prod"

You can place the `.env` file:

- inside `py/examples/`
- inside `c/examples/`
- or anywhere where you run your program

The clients search for `.env` inside the directory your program is running in.

You will receive your API key from the maintainers of the LLMProxy

------------------------------------------------------------------------

## Getting Started

1. Acquire an API key for your LLMProxy backend.
2. Create a `.env` file with the required variables.
3. Choose a language and follow its README:
   - [`py/README.md`](py/README.md)
   - [`c/README.md`](c/README.md)
4. Run an example program (e.g., `generate` or `model_info`) to verify your setup.

------------------------------------------------------------------------

# Testing Guide for CS112 Proxy with LLM Support

## Overview
The proxy now supports two modes:
- **Simple Mode** (`llm=false` or no parameter): Fast pass-through with only `X-Proxy:CS112` header injection
- **LLM Mode** (`llm=true`): Intelligent HTML enhancement with AI analysis

## Testing Simple Mode (llm=false)

### Start the proxy:
```bash
./proxy 8080 proxy_ca.crt proxy_ca.key
```
OR explicitly:
```bash
./proxy 8080 proxy_ca.crt proxy_ca.key llm=false
```

### Expected behavior:
- Fast interception and forwarding
- Only injects `X-Proxy:CS112` header
- No gzip processing
- No Flask communication
- No file saving
- **This should work exactly like the original proxy**

### Configure browser:
1. Set HTTP/HTTPS proxy to `127.0.0.1:8080`
2. Import `proxy_ca.crt` as a trusted CA
3. Access any webpage (http://example.com, https://www.wikipedia.org, etc.)
4. Should work fast and transparently

## Testing LLM Mode (llm=true)

### Prerequisites:
1. Start Flask service first:
```bash
cd /home/jeffliu/project/LLMProxy-cs112/LLMProxy-cs112
python3 fake_news.py
```

2. Verify Flask is running at http://127.0.0.1:5000

### Start the proxy with LLM:
```bash
./proxy 8080 proxy_ca.crt proxy_ca.key llm=true
```

### Expected behavior:
- Removes `Accept-Encoding` header from requests (to avoid gzip)
- Collects full HTTP response
- Checks if response is `text/html`
- If HTML:
  - Sends to Flask `/enhance` endpoint
  - Flask injects JavaScript for async LLM analysis
  - Returns enhanced HTML with analysis banner
- If not HTML or Flask unavailable:
  - Falls back to simple header injection

### Configure browser:
1. Set HTTP/HTTPS proxy to `127.0.0.1:8080`
2. Import `proxy_ca.crt` as a trusted CA
3. Access news websites (https://www.wikipedia.org, https://news.ycombinator.com, etc.)
4. Should see:
   - Page loads immediately
   - "⏳ Generating AI analysis..." banner appears
   - After a few seconds, banner updates with AI summary and fake news detection

## Testing Flask Service Standalone

### Test mode (no proxy needed):
```bash
python3 fake_news.py test
```

This will read content from `fake_news_test.txt` and run LLM analysis.

## Troubleshooting

### Simple Mode Issues:
- If pages don't load: Check if port 8080 is free
- If certificate errors: Ensure `proxy_ca.crt` is properly imported in browser
- If header not injected: Check proxy is running and browser is configured

### LLM Mode Issues:
- If no AI banner appears: Check Flask is running (`curl http://127.0.0.1:5000`)
- If banner stuck on "Generating...": Check LLM API keys in `.env` file
- If "Flask not available": Start Flask service first
- If compressed content: Proxy removes Accept-Encoding, but some servers may still send gzip

### Performance:
- **Simple mode**: Very fast, minimal latency
- **LLM mode**: Slight delay for HTML pages (Flask injection ~200-500ms), but page displays immediately before AI analysis

## Key Differences

| Feature | Simple Mode | LLM Mode |
|---------|-------------|----------|
| Speed | Very fast | Slight delay for HTML |
| Memory | Low | Higher (collects full response) |
| Flask | Not used | Required for HTML |
| AI Analysis | None | Async summary + fake news detection |
| Gzip Handling | Pass-through | Requests uncompressed |
| Use Case | General browsing | News/content analysis |

## Code Structure

- `llm_enabled` global variable controls mode
- `handle_https_connect()` has two code paths based on `llm_enabled`
- `handle_http_request()` has two code paths based on `llm_enabled`
- Flask communication via `send_to_flask_enhance()`
- Helper functions: `is_html_content_type()`, `extract_content_type()`

