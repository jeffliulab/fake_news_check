#!/usr/bin/env python3
"""
CS112 Final Project - Fake News Detection Service
Flask server that provides AI summary and fake news detection
Main scripts are created by Pang Liu,
Debug and print information are created with the help of Gemini (AI Tools).
"""

import sys
import os
import base64
import json
from datetime import datetime
from flask import Flask, request, jsonify, Response
from flask_cors import CORS

# 将 py 目录添加到 Python 路径
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'py'))

from llmproxy import LLMProxy
from dotenv import load_dotenv

# 加载 .env
load_dotenv()

# 创建 LLMProxy 客户端实例
client = LLMProxy()

# Flask app
app = Flask(__name__)
CORS(app)

# 服务器配置
FLASK_HOST = '127.0.0.1'
FLASK_PORT = 5000


@app.route('/api/summary', methods=['GET', 'POST'])
def get_summary():
    """
    生成 AI 总结 + Fake News 检测
    由浏览器中的 JavaScript 异步调用
    
    Returns:
    {
        "summary": "网页总结文本",
        "is_fake_news": true/false,
        "confidence": "HIGH/MEDIUM/LOW"
    }
    """
    try:
        # 支持 GET 和 POST
        if request.method == 'POST':
            data = request.get_json()
            page_url = data.get('url', '')
            page_content = data.get('content', '')
        else:
            page_url = request.args.get('url', '')
            page_content = request.args.get('content', '')
        
        print("")
        print("🤖" + "=" * 58 + "🤖")
        print("📋 JavaScript Async Request: Summary + Fake News Detection")
        print("=" * 60)
        print(f"   URL: {page_url[:70] if page_url else '(none)'}")
        print(f"   Content length: {len(page_content)} chars")
        print("   Preparing to call LLM Proxy...")
        print("=" * 60)
        
        if not page_content or len(page_content) < 50:
            print("⚠️  Content too short, returning default message")
            return jsonify({
                'summary': 'Page content insufficient to generate summary.',
                'is_fake_news': False,
                'confidence': 'N/A'
            })
        
        # 调用 LLM 进行总结 + Fake News 检测
        summary, is_fake, confidence = analyze_content(page_content)
        
        print("")
        print("📋 LLM Analysis Complete")
        print("=" * 60)
        print(f"✅ Summary: {summary[:100]}...")
        print(f"🔍 Fake News: {'Yes' if is_fake else 'No'} (Confidence: {confidence})")
        print("🤖" + "=" * 58 + "🤖")
        print("")
        
        return jsonify({
            'summary': summary,
            'is_fake_news': is_fake,
            'confidence': confidence,
            'url': page_url
        })
    
    except Exception as e:
        print(f"❌ Analysis failed: {e}")
        return jsonify({
            'error': str(e),
            'summary': 'Analysis failed',
            'is_fake_news': False,
            'confidence': 'N/A'
        }), 200


@app.route('/enhance', methods=['POST'])
def enhance_html():
    """
    注入 JavaScript 脚本到 HTML，异步加载 AI 总结和 Fake News 检测
    """
    try:
        data = request.get_json(force=True)
        
        if not data:
            return jsonify({'error': 'Invalid JSON'}), 400
        
        # Check if HTML is base64 encoded
        if 'html_base64' in data:
            html_content = base64.b64decode(data['html_base64']).decode('utf-8', errors='replace')
            original_url = data.get('url', '')
            print(f"[ENHANCE] Received HTML: {len(html_content)} bytes, URL: {original_url}")
        elif 'html' in data:
            html_content = data['html']
            original_url = data.get('url', '')
        else:
            return jsonify({'error': 'Missing html or html_base64'}), 400
        
        # Inject JavaScript
        modified_html = inject_async_summary_script(html_content, original_url)
        
        print(f"[ENHANCE] ✅ Quick return HTML ({len(modified_html)} bytes), JS will load summary asynchronously")
        
        # 使用 base64 编码传输
        html_base64 = base64.b64encode(modified_html.encode('utf-8')).decode('ascii')
        
        response_json = json.dumps({'html_base64': html_base64}, ensure_ascii=True)
        
        return Response(response_json, mimetype='application/json')
    
    except Exception as e:
        print(f"[ERROR] Page processing failed: {e}")
        return jsonify({'error': str(e)}), 500


def analyze_content(text):
    """
    Use LLM to analyze content: generate summary + detect Fake News
    
    Returns:
        (summary, is_fake_news, confidence)
    """
    if not client:
        return "⚠️ LLM not configured.", False, "N/A"
    
    try:
        # Limit input length
        text = text[:3000]
        
        print(f"[LLM] Analyzing... (input: {len(text)} chars)")
        
        import time
        start_time = time.time()
        
        # Construct Prompt: complete summary + Fake News detection
        system_prompt = """You are a web content analysis assistant.
Your tasks are:
1. Summarize the main content of the webpage in English (no more than 100 words)
2. Determine if the content contains fake news

Please return in the following JSON format:
{
  "summary": "Content summary in English",
  "is_fake_news": true or false,
  "confidence": "HIGH" or "MEDIUM" or "LOW"
}

Criteria for Fake News detection:
- Exaggerated or false statements
- Lack of reliable sources
- Clickbait or misleading information
- Obvious bias or political propaganda

Only return JSON, no other content."""
        
        # 调用 LLM
        response = client.generate(
            model='4o-mini',
            system=system_prompt,
            query=text,
            temperature=0.3,
            lastk=0
        )
        
        elapsed = time.time() - start_time
        result_text = response['result'].strip()
        
        print(f"[LLM] ✅ Analysis complete (time: {elapsed:.2f}s)")
        
        # Parse JSON response
        try:
            result_json = json.loads(result_text)
            summary = result_json.get('summary', result_text[:100])
            is_fake = result_json.get('is_fake_news', False)
            confidence = result_json.get('confidence', 'MEDIUM')
        except:
            # If not JSON, use entire result as summary
            summary = result_text[:150]
            is_fake = False
            confidence = 'N/A'
        
        return summary, is_fake, confidence
        
    except Exception as e:
        print(f"[LLM] ❌ Analysis failed: {e}")
        return "❌ Analysis failed, please try again later.", False, "N/A"


def inject_async_summary_script(html_content, page_url):
    """
    注入轻量级 JavaScript 脚本，异步加载 AI 总结 + Fake News 检测
    
    Args:
        html_content: 原始 HTML
        page_url: 页面 URL
    
    Returns:
        修改后的 HTML（添加了 JS 脚本）
    """
    # 创建异步加载脚本
    async_script = f'''
<script>
// CS112 AI Summary + Fake News Detection - Async Loader
(function() {{
    if (document.readyState === 'loading') {{
        document.addEventListener('DOMContentLoaded', initAISummary);
    }} else {{
        setTimeout(initAISummary, 100);
    }}
    
    function initAISummary() {{
        try {{
            // Create top banner (loading state)
            createBanner('⏳ Generating AI analysis...', null, null);
            
            // Extract page text content
            var pageText = extractPageText();
            
            // Async request to Flask for summary
            requestSummary(pageText);
        }} catch(e) {{
            console.error('[AI Summary] Error:', e);
        }}
    }}
    
    function createBanner(message, isFakeNews, confidence) {{
        var banner = document.createElement('div');
        banner.id = 'cs112-ai-summary-banner';
        
        // Select color based on Fake News detection result
        var bgColor = '#667eea';  // Default blue-purple
        var fakeNewsHtml = '';
        
        if (isFakeNews !== null) {{
            if (isFakeNews) {{
                bgColor = '#e74c3c';  // Red - Fake News
                fakeNewsHtml = `
                    <div style="background: #ffe6e6; border: 2px solid #e74c3c; border-radius: 8px; padding: 15px; margin-top: 15px;">
                        <div style="display: flex; align-items: center; margin-bottom: 10px;">
                            <span style="font-size: 32px; margin-right: 10px;">⚠️</span>
                            <div>
                                <h3 style="margin: 0; color: #e74c3c; font-size: 18px; font-weight: bold;">Fake News Warning</h3>
                                <p style="margin: 5px 0 0 0; color: #c0392b; font-size: 14px;">This content may contain false or misleading information</p>
                            </div>
                        </div>
                        <p style="margin: 0; color: #e74c3c; font-size: 13px;">
                            <strong>Confidence:</strong> ${{confidence || 'MEDIUM'}}
                        </p>
                    </div>
                `;
            }} else {{
                bgColor = '#27ae60';  // Green - Reliable content
                fakeNewsHtml = `
                    <div style="background: #e8f8f5; border: 2px solid #27ae60; border-radius: 8px; padding: 12px; margin-top: 15px;">
                        <div style="display: flex; align-items: center;">
                            <span style="font-size: 24px; margin-right: 10px;">✅</span>
                            <div>
                                <p style="margin: 0; color: #27ae60; font-size: 14px; font-weight: bold;">Content Reliable</p>
                                <p style="margin: 3px 0 0 0; color: #1e8449; font-size: 12px;">No fake news detected</p>
                            </div>
                        </div>
                    </div>
                `;
            }}
        }}
        
        banner.innerHTML = `
            <div style="all: initial; display: block; width: 100%; background: linear-gradient(135deg, ${{bgColor}} 0%, ${{bgColor}}dd 100%); padding: 0; margin: 0; box-sizing: border-box; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; position: relative; z-index: 999999;">
                <div style="max-width: 1200px; margin: 0 auto; padding: 20px; background: rgba(255, 255, 255, 0.97); box-shadow: 0 2px 10px rgba(0,0,0,0.1);">
                    <div style="display: flex; justify-content: space-between; align-items: flex-start; flex-wrap: wrap;">
                        <div style="flex: 1; min-width: 300px; margin-right: 20px;">
                            <h2 style="margin: 0 0 15px 0; padding: 0; font-size: 24px; font-weight: 700; color: ${{bgColor}}; display: flex; align-items: center;">
                                <span style="margin-right: 10px; font-size: 28px;">🤖</span>
                                <span>AI Content Analysis</span>
                            </h2>
                            <div id="cs112-summary-content" style="background: #f8f9fa; border-left: 4px solid ${{bgColor}}; padding: 15px; border-radius: 8px; margin-bottom: 10px;">
                                <p style="margin: 0; padding: 0; font-size: 16px; line-height: 1.8; color: #333;">
                                    ${{message}}
                                </p>
                            </div>
                            ${{fakeNewsHtml}}
                            <div style="display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; font-size: 13px; color: #666; margin-top: 15px;">
                                <span>💡 <strong>Powered by LLM Proxy</strong> | CS112 SafeGate</span>
                                <button onclick="document.getElementById('cs112-ai-summary-banner').remove()" style="background: #95a5a6; color: white; border: none; padding: 8px 16px; border-radius: 5px; cursor: pointer; font-size: 14px; font-weight: 600;">Close</button>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        `;
        
        if (document.body) {{
            document.body.insertBefore(banner, document.body.firstChild);
        }}
    }}
    
    function updateBanner(message, isFakeNews, confidence) {{
        // Remove old banner
        var oldBanner = document.getElementById('cs112-ai-summary-banner');
        if (oldBanner) {{
            oldBanner.remove();
        }}
        // Create new banner
        createBanner(message, isFakeNews, confidence);
    }}
    
    function extractPageText() {{
        var text = document.body.innerText || document.body.textContent || '';
        return text.substring(0, 3000);
    }}
    
    function requestSummary(pageText) {{
        var url = 'http://127.0.0.1:5000/api/summary';
        
        fetch(url, {{
            method: 'POST',
            headers: {{
                'Content-Type': 'application/json'
            }},
            body: JSON.stringify({{
                url: '{page_url}',
                content: pageText
            }})
        }})
        .then(response => response.json())
        .then(data => {{
            if (data.summary) {{
                updateBanner(data.summary, data.is_fake_news, data.confidence);
                console.log('[AI Summary] ✅ Analysis complete');
                console.log('[AI Summary] Fake News:', data.is_fake_news, 'Confidence:', data.confidence);
            }} else {{
                updateBanner('❌ Analysis failed', null, null);
            }}
        }})
        .catch(error => {{
            console.error('[AI Summary] Request failed:', error);
            updateBanner('⚠️ Cannot connect to AI server', null, null);
        }});
    }}
}})();
</script>
'''
    
    # 在 <body> 标签后插入脚本
    body_pos = html_content.find('<body')
    if body_pos != -1:
        body_end = html_content.find('>', body_pos)
        if body_end != -1:
            before = html_content[:body_end+1]
            after = html_content[body_end+1:]
            return before + async_script + after
    
    # 在 <html> 标签后插入
    html_pos = html_content.find('<html')
    if html_pos != -1:
        html_end = html_content.find('>', html_pos)
        if html_end != -1:
            before = html_content[:html_end+1]
            after = html_content[html_end+1:]
            return before + async_script + after
    
    # 直接放在最前面
    return async_script + html_content


def run_test_mode():
    """Run test mode, read content from file and call LLM"""
    test_file_path = "fake_news_test.txt"
    
    print(f"{'='*60}")
    print("🧪 LLM Test Mode")
    print(f"{'='*60}")
    print("✅ LLM Proxy client initialized")
    print(f"🤖 Model: 4o-mini")
    print(f"{'='*60}\n")

    try:
        with open(test_file_path, 'r', encoding='utf-8') as f:
            content_to_analyze = f.read()
        
        print(f"📂 Reading test file: {os.path.abspath(test_file_path)}")
        print(f"✅ Successfully read {len(content_to_analyze)} chars\n")
        
        print("📄 File content:")
        print(f"{'-'*60}")
        print(content_to_analyze[:500])
        if len(content_to_analyze) > 500:
            print("...")
        print(f"{'-'*60}\n")

        print("⏳ Calling LLM Proxy for content analysis...\n")
        
        # Call analysis function
        summary, is_fake, confidence = analyze_content(content_to_analyze)
        
        print("✅ LLM analysis successful\n")
        print("📝 Analysis Results:")
        print(f"{'='*60}")
        print(f"📄 Summary: {summary}")
        print(f"🔍 Fake News: {'Yes' if is_fake else 'No'}")
        print(f"📊 Confidence: {confidence}")
        print(f"{'='*60}\n")
        
    except FileNotFoundError:
        print(f"❌ Error: Test file '{test_file_path}' not found. Please create the file with test content.")
    except Exception as e:
        sys.stderr.write(f"❌ LLM test mode error: {str(e)}\n")
    finally:
        print("✅ Test complete")


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1].lower() == 'test':
        run_test_mode()
    else:
        print("")
        print("🚀" + "=" * 58 + "🚀")
        print("   CS112 Fake News Detection Service")
        print("🚀" + "=" * 58 + "🚀")
        print("")
        print(f"🌐 Server Address: http://{FLASK_HOST}:{FLASK_PORT}")
        print(f"🤖 LLM Model: 4o-mini")
        print("")
        print("📋 Active Features:")
        print("   ✅ AI Summary (Async loading)")
        print("   ✅ Fake News Detection")
        print("")
        print("💡 When a page is accessed:")
        print("   1️⃣  Proxy sends HTML to Flask")
        print("   2️⃣  Flask injects JavaScript")
        print("   3️⃣  Page displays immediately")
        print("   4️⃣  JavaScript requests AI analysis")
        print("   5️⃣  Banner appears with results")
        print("")
        print("=" * 60)
        print("🎯 Ready! Waiting for requests...")
        print("=" * 60)
        print("")
        
        app.run(host=FLASK_HOST, port=FLASK_PORT, debug=True, threaded=True)
