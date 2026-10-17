"""Multi-view public Angular/React-admin regressions. Internet required.

Usage: python tests/public_demos.py /path/to/webcli [all|realworld|react-admin]
Failures, including missing APIs, fail the test; loading shells are not passes.
Each scenario uses a fresh profile. No real accounts, uploads or saved edits.
"""
import sys
from urllib.parse import urlsplit

from public_browser import PublicBrowser


def realworld(browser):
    base = 'https://demo.realworld.show/'
    page = browser.run('open', base)
    document = page['documentId']
    page = browser.run('wait', 'text', 'Popular Tags', '--timeout', '15000')
    articles = [item for item in page['links'] if '/article/' in item['url']]
    assert articles, 'Feed returned no actionable articles'
    article = articles[0]
    page = browser.run('click', article['ref'])
    browser.same_document(page, document)
    assert page['url'] == article['url'] and 'Read more...' not in page['text'], page
    author = next((item for item in page['links'] if '/profile/' in item['url']), None)
    assert author is not None, 'Article author not actionable'
    page = browser.run('click', author['ref'])
    browser.same_document(page, document)
    assert page['url'] == author['url'], page
    assert urlsplit(author['url']).path.split('/')[-1] in page['text'], page
    page = browser.action(page, 'Home')
    page = browser.run('wait', 'text', 'Popular Tags', '--timeout', '15000')
    tag = next((item for item in page['links'] if '/tag/' in item['url']), None)
    assert tag is not None, 'Feed returned no actionable tags'
    page = browser.run('click', tag['ref'])
    browser.same_document(page, document)
    assert page['url'] == tag['url'] and tag['name'] in page['text'], page
    assert any('/article/' in item['url'] for item in page['links']), 'Filtered feed has no articles'
    page = browser.action(page, 'Sign in')
    browser.same_document(page, document)
    assert urlsplit(page['url']).path == '/login', page
    page = browser.fill(page, 'Email', 'webcli-regression@example.invalid')
    assert any(item['name'] == 'Email' and item['value'] == 'webcli-regression@example.invalid'
               for item in page['fields']), page
    page = browser.action(page, 'Need an account?')
    assert urlsplit(page['url']).path == '/register', page
    page = browser.fill(page, 'Username', 'WebCLI regression')
    page = browser.fill(page, 'Email', 'webcli-regression@example.invalid')
    assert any(item['type'] == 'password' for item in page['fields']), page
    page = browser.action(page, 'Have an account?')
    assert urlsplit(page['url']).path == '/login', page
    page = browser.action(page, 'Home')
    page = browser.run('wait', 'text', 'Popular Tags', '--timeout', '15000')
    browser.same_document(page, document)
    assert any('/article/' in item['url'] for item in page['links']), page


def react_admin(browser):
    base = 'https://marmelab.com/react-admin-demo/'
    page = browser.run('open', base)
    page = browser.run('wait', 'text', 'Username', '--timeout', '15000')
    document = page['documentId']
    page = browser.fill(page, 'Username *', 'demo')
    page = browser.fill(page, 'Password *', 'demo')
    page = browser.action(page, 'Sign in')
    page = browser.run('wait', 'text', 'Customers', '--timeout', '15000')
    browser.same_document(page, document)
    page = browser.action(page, 'See all customers')
    page = browser.run('wait', 'selector', 'input', '--timeout', '15000')
    assert 'customers' in page['url'].lower() and page['fields'], page
    search = next((item for item in page['fields'] if 'search' in item['name'].lower()), None)
    assert search is not None, 'Customer list has no search field'
    page = browser.run('fill', search['ref'], 'unlikely-webcli-customer-987654')
    page = browser.run('wait', 'text', 'No Customers found', '--timeout', '15000')
    browser.same_document(page, document)
    page = browser.run('fill', search['ref'], '')
    page = browser.run('wait', 'idle', '--timeout', '15000')
    details = next((item for item in page['links'] if '/customers/' in item['url']), None)
    assert details is not None, 'No customer detail/edit action'
    page = browser.run('click', details['ref'])
    assert 'customers' in page['url'].lower() and page['fields'], page
    page = browser.action(page, 'Posters')
    assert 'products' in page['url'].lower(), page
    page = browser.action(page, 'Orders')
    assert 'commands' in page['url'].lower() or 'orders' in page['url'].lower(), page
    browser.same_document(page, document)


if __name__ == '__main__':
    scenarios = {'realworld': realworld, 'react-admin': react_admin}
    selection = sys.argv[2] if len(sys.argv) > 2 else 'all'
    assert selection == 'all' or selection in scenarios, 'Unknown scenario'
    failures = []
    for name, scenario in scenarios.items():
        if selection != 'all' and name != selection:
            continue
        try:
            with PublicBrowser(sys.argv[1]) as browser:
                scenario(browser)
                print(f'PASS {name}: {browser.commands} commands across multiple SPA views', flush=True)
        except Exception as error:
            failures.append(name)
            print(f'FAIL {name} at command {browser.commands} ({browser.last_action}): {error}', flush=True)
    raise SystemExit(1 if failures else 0)
