"""Native CLI back/forward workflows across documents, SPA states and fragments."""
import http.server
import json
import sys
import threading

from public_browser import PublicBrowser


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        if self.path == '/first':
            body = '<title>First</title><body>FIRST<input aria-label=Saved><a href=/second>Next page</a></body>'
        elif self.path == '/spa':
            body = '''<title>SPA</title><body><p id=route></p><input aria-label=Memory>
<button onclick="go('two')">Step two</button><button onclick="go('three')">Step three</button>
<a href=#details>Details</a><script>
let pops=0;history.replaceState({step:'one'},'',location.href);
function render(){document.getElementById('route').textContent='ROUTE='+location.pathname+';STATE='+(history.state?.step??'none')+';POPS='+pops+';HASH='+location.hash}
function go(step){history.pushState({step},'','/spa/'+step);render()}
addEventListener('popstate',()=>{pops++;render()});addEventListener('hashchange',render);render();
</script></body>'''
        else:
            body = '<title>Destination</title><body>' + self.path + '</body>'
        payload = body.encode()
        self.send_response(200)
        self.send_header('Content-Type', 'text/html; charset=utf-8')
        self.send_header('Content-Length', str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)


def failure(browser, code, *arguments):
    result = browser.terminal.run([*arguments, '--json'])
    page = json.loads(result.stdout)
    assert result.returncode != 0 and page['error'] == code, page


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
base = f'http://127.0.0.1:{server.server_port}'
try:
    with PublicBrowser(sys.argv[1]) as browser:
        failure(browser, 'NO_PAGE', 'back')
        failure(browser, 'NO_PAGE', 'forward')
        failure(browser, 'INVALID_ARGUMENT', 'back', 'unexpected')
        first = browser.run('open', base + '/first')
        assert browser.run('back')['documentId'] == first['documentId']
        first = browser.fill(first, 'Saved', 'retained value')
        second = browser.action(first, 'Next page')
        assert second['documentId'] != first['documentId'] and second['url'] == base + '/second', second
        failure(browser, 'STALE_DOCUMENT', 'back', '--document', first['documentId'])
        restored = browser.run('back', '--document', second['documentId'])
        assert restored['documentId'] == first['documentId'] and restored['url'] == first['url'], restored
        assert restored['fields'][0]['value'] == 'retained value', restored
        assert browser.run('forward')['documentId'] == second['documentId']
        assert browser.run('forward')['documentId'] == second['documentId']
        browser.run('back')
        third = browser.run('open', base + '/third')
        assert browser.run('forward')['documentId'] == third['documentId'], 'New navigation did not discard forward entries'
        assert browser.run('back')['documentId'] == first['documentId']
        print('PASS back/forward: documents, retained form state, boundaries, forward branch discard and document guards', flush=True)

        page = browser.run('open', base + '/spa')
        document = page['documentId']
        page = browser.fill(page, 'Memory', 'survives routing')
        page = browser.action(page, 'Step two')
        page = browser.action(page, 'Step three')
        for command, step, pops in [('back', 'two', 1), ('back', 'one', 2), ('forward', 'two', 3)]:
            page = browser.run(command)
            browser.same_document(page, document)
            assert f'STATE={step};POPS={pops}' in page['text'], page
            assert page['url'] == base + ('/spa' if step == 'one' else '/spa/' + step), page
            assert page['fields'][0]['value'] == 'survives routing', page
        page = browser.action(page, 'Details')
        assert page['url'].endswith('#details'), page
        page = browser.run('back')
        browser.same_document(page, document)
        assert page['url'] == base + '/spa/two' and ';HASH=#details' not in page['text'], page
        assert 'STATE=two;' in page['text'], page
        page = browser.run('forward')
        browser.same_document(page, document)
        assert page['url'] == base + '/spa/two#details' and ';HASH=#details' in page['text'], page
        assert 'STATE=none;' in page['text'], page
        print('PASS back/forward: SPA popstate, state restoration, fragment traversal and unchanged document', flush=True)
finally:
    server.shutdown()
