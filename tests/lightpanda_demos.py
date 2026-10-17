"""Native CLI workflows against Lightpanda's public, independently authored demos.

Usage: python tests/lightpanda_demos.py /path/to/webcli [all|campfire|amiibo|oracle]
Requires internet. No accounts, purchases, uploads, or third-party Python packages.
"""
import json
import sys
from urllib.parse import urljoin, urlsplit
from urllib.request import Request, urlopen

from public_browser import PublicBrowser

BASE = 'https://demo-browser.lightpanda.io/'


def fixture(path):
    # Read the demo's public data independently of WebCLI's DOM extraction.
    request = Request(urljoin(BASE, path), headers={'User-Agent': 'Mozilla/5.0'})
    with urlopen(request, timeout=15) as response:
        return json.load(response)


def canonical_url(url):
    with urlopen(Request(url, headers={'User-Agent': 'Mozilla/5.0'}), timeout=15) as response:
        return response.geturl()


def campfire(browser):
    product = fixture('campfire-commerce/json/product.json')
    reviews = fixture('campfire-commerce/json/reviews.json')
    assert reviews, 'Demo fixture no longer contains reviews'
    page = browser.run('open', BASE + 'campfire-commerce/')
    document = page['documentId']
    page = browser.run('wait', 'text', reviews[-1][:60], '--timeout', '10000')
    assert product['name'] in page['text'] and product['description'] in page['text'], page
    assert str(product['price']) in page['text'], page
    assert all(feature in page['text'] for feature in product['features']), page
    assert all(item['name'] in page['text'] for item in product['related']), page
    assert all(review in page['text'] for review in reviews), page
    assert 'Loading...' not in page['text'], page
    quantity = next(item['ref'] for item in page['fields'] if item['type'] == 'number')
    page = browser.run('fill', quantity, '3')
    assert any(item['ref'] == quantity and item['value'] == '3' for item in page['fields']), page
    page = browser.action(page, 'Products')
    browser.same_document(page, document)
    assert any(item['ref'] == quantity and item['value'] == '3' for item in page['fields']), page
    page = browser.run('observe')
    assert product['name'] in page['text'] and reviews[-1] in page['text'], page


def amiibo(browser):
    page = browser.run('open', BASE + 'amiibo/')
    page = browser.run('wait', 'selector', '#nav a', '--timeout', '10000')
    assert page['title'] == fixture('amiibo/03000502.json')['amiibo']['name'], page
    # Cross three real documents, using only the controls returned by each page.
    for direction in ('Next', 'Previous', None):
        link = next(item for item in page['links']
                    if item['name'] == direction) if direction else next(
                        item for item in page['links'] if item['name'] not in ('Next', 'Previous'))
        destination = link['url']
        path = urlsplit(destination).path
        expected = fixture('amiibo/03000502.json' if path.endswith('/index.html') else
                           path.rsplit('.', 1)[0] + '.json')
        canonical = canonical_url(destination)
        previous = page['documentId']
        old_ref = link['ref']
        page = browser.run('click', old_ref)
        page = browser.run('wait', 'selector', '#nav a', '--timeout', '10000')
        assert page['url'] == canonical and page['documentId'] != previous, page
        assert page['title'] == expected['amiibo']['name'], page
        assert expected['amiibo']['name'] in page['text'], page
        assert all(item['ref'] != old_ref for item in page['links']), 'Navigation reused an old ref'


def oracle(browser):
    page = browser.run('open', BASE + 'magic8ball/')
    document = page['documentId']
    for index, question in enumerate(('Will the worker answer?', 'Can I ask a second question?')):
        field = next(item['ref'] for item in page['fields'] if item['type'] == 'text')
        page = browser.run('fill', field, question)
        page = browser.action(page, 'Ask') if index == 0 else browser.run('keyboard', field, 'Enter')
        page = browser.run('wait', 'text', 'the spirits whisper:', '--timeout', '10000')
        browser.same_document(page, document)
        texts = [item['text'] for item in page['content']]
        answer = texts[texts.index('Answer:') + 1]
        assert answer not in ('...', 'ask me anything'), page
        assert 'the spirits whisper: ' + answer in page['text'], page
        assert any(item['value'] == question for item in page['fields']), page


if __name__ == '__main__':
    scenarios = {'campfire': campfire, 'amiibo': amiibo, 'oracle': oracle}
    selection = sys.argv[2] if len(sys.argv) > 2 else 'all'
    assert selection == 'all' or selection in scenarios, 'Unknown scenario'
    failures = []
    for name, scenario in scenarios.items():
        if selection not in ('all', name):
            continue
        try:
            with PublicBrowser(sys.argv[1]) as browser:
                scenario(browser)
                print(f'PASS {name}: {browser.commands} native CLI commands', flush=True)
        except Exception as error:
            failures.append(name)
            print(f'FAIL {name} at command {browser.commands} ({browser.last_action}): {error}', flush=True)
    raise SystemExit(1 if failures else 0)
