"""Run native and JavaScript dialog interactions on independent public demos.

Usage: python tests/public_modals.py /path/to/webcli
       python tests/public_modals.py /path/to/webcli x-refs
These checks need internet access; no accounts or remote submissions are used.
"""
import sys
import json
import time
from public_browser import PublicBrowser

def all_buttons(browser, page):
    buttons = list(page['buttons'])
    while page['pagination']['nextOffset'] is not None:
        result = browser.terminal.run(['observe', '--json', '--offset',
                                      str(page['pagination']['nextOffset']), '--limit', '200'])
        page = json.loads(result.stdout)
        assert result.returncode == 0 and not page.get('scriptErrors'), page
        buttons.extend(page['buttons'])
    return {item['ref']: item for item in buttons}.values()

with PublicBrowser(sys.argv[1]) as browser:
    if len(sys.argv) > 2 and sys.argv[2] in ('x', 'x-refs'):
        page = browser.run('open', 'https://x.com/')
        document = page['documentId']
        field = next(item for item in page['fields'] if item['type'] == 'text')
        value = 'webcli-regression@example.invalid'
        if sys.argv[2] == 'x-refs':
            # Exercise a human/agent delay between reading the server-rendered
            # reference and acting after client hydration. No extra engine wait.
            time.sleep(15)
        page = browser.run('fill', field['ref'], value)
        browser.same_document(page, document)
        assert page['url'] == 'https://x.com/', 'Fill navigated away from the login page'
        assert any(item.get('value') == value for item in page['fields']), 'Hydration lost fill'
        if sys.argv[2] == 'x-refs':
            assert not any(item['type'] == 'password' for item in page['fields']), \
                'Invisible password input leaked into the landing-page observation'
            hydrated = next(item['ref'] for item in page['fields'] if item['type'] == 'text')
            page = browser.run('fill', hydrated, 'second-webcli-check@example.invalid')
            browser.same_document(page, document)
            assert any(item['ref'] == hydrated and item['value'] == 'second-webcli-check@example.invalid'
                       for item in page['fields']), 'Repeated fill replaced the hydrated control'
            print('PASS public X: delayed recovery and repeated fill retain document/control (no submission)', flush=True)
            sys.exit(0)
        page = browser.action(page, 'Continue')
        assert any(item['name'] == 'Continue with phone' for item in page['buttons']), page
        assert "See what's happening" in page['text'], page
        assert 'Happening now.' not in page['text'], 'Modal observation includes its background'
        assert any(item['name'] == 'Outside dialog' for item in page['buttons']), page
        page = browser.action(page, 'Outside dialog')
        assert not any(item['name'] == 'Outside dialog' for item in page['buttons']), page
        assert 'Happening now.' in page['text'], page
        print('PASS public X: onboarding modal visibility and outside dismissal (not email verification)', flush=True)
        sys.exit(0)
    page = browser.run('open', 'https://googlechrome.github.io/samples/dialog/')
    document = page['documentId']
    assert not any(item['name'] == 'Close' for item in page['buttons']), page
    page = browser.action(page, 'Open Dialog')
    assert any(item['name'] == 'Close' for item in page['buttons']), page
    page = browser.action(page, 'Close')
    assert not any(item['name'] == 'Close' for item in page['buttons']), page
    browser.same_document(page, document)
    print('PASS public native dialog: open and close', flush=True)

    page = browser.run('open', 'https://getbootstrap.com/docs/5.3/components/modal/')
    document = page['documentId']
    # The documentation contains other static Close buttons. Track the new
    # references produced by opening the live example rather than guessing one.
    buttons = list(all_buttons(browser, page))
    previous = {item['ref'] for item in buttons}
    launcher = next(item['ref'] for item in buttons if item['name'] == 'Launch demo modal')
    page = browser.run('click', launcher)
    assert 'Woo-hoo' in page['text'], page
    assert not any(item['name'] == 'Launch demo modal' for item in all_buttons(browser, page)), 'Background controls leaked into modal'
    dismiss = next((item['ref'] for item in all_buttons(browser, page)
                    if item['name'] == 'Close' and item['ref'] not in previous), None)
    assert dismiss is not None, 'Live Bootstrap dialog did not expose its dismiss button'
    page = browser.run('click', dismiss)
    assert not any(item['ref'] == dismiss for item in all_buttons(browser, page)), page
    browser.same_document(page, document)
    # Reopen the same instance: outside dismissal must run the page's handlers,
    # not just work for the initial open or a special Close button.
    page = browser.run('click', launcher)
    page = browser.action(page, 'Outside dialog')
    assert 'Woo-hoo' not in page['text'], 'Bootstrap outside dismissal failed'
    assert not any(item['name'] == 'Outside dialog' for item in all_buttons(browser, page)), page
    page = browser.run('click', launcher)
    outside = next(item['ref'] for item in page['buttons'] if item['name'] == 'Outside dialog')
    page = browser.run('keyboard', outside, 'Escape')
    page = browser.run('wait', 'idle', '--timeout', '5000')
    assert 'Woo-hoo' not in page['text'], 'Bootstrap Escape dismissal failed'
    browser.same_document(page, document)
    print('PASS public Bootstrap modal: background isolation, Close, outside, Escape and reopen', flush=True)

    static_launcher = next(item['ref'] for item in all_buttons(browser, page)
                           if item['name'] == 'Launch static backdrop modal')
    page = browser.run('click', static_launcher)
    assert 'I will not close' in page['text'], page
    page = browser.action(page, 'Outside dialog')
    assert 'I will not close' in page['text'], 'Static backdrop was forcibly dismissed'
    outside = next(item['ref'] for item in page['buttons'] if item['name'] == 'Outside dialog')
    page = browser.run('keyboard', outside, 'Escape')
    page = browser.run('wait', 'idle', '--timeout', '5000')
    assert 'I will not close' in page['text'], 'Static dialog ignored its keyboard policy'
    page = browser.action(page, 'Close')
    assert 'I will not close' not in page['text'], page
    browser.same_document(page, document)
    print('PASS public Bootstrap static modal: outside/Escape refused, Close allowed', flush=True)

    launchers = {item['name']: item['ref'] for item in all_buttons(browser, page)}
    for recipient in ('@mdo', '@fat'):
        page = browser.run('click', launchers['Open modal for ' + recipient])
        assert 'New message to ' + recipient in page['text'], page
        assert next(item['value'] for item in page['fields']
                    if item['name'] == 'Recipient:') == recipient, page
        page = browser.fill(page, 'Recipient:', '@webcli-test')
        page = browser.fill(page, 'Message:', 'Public modal input check')
        assert any(item.get('value') == 'Public modal input check'
                   for item in page['fields']), page
        assert not any(item['name'].startswith('Open modal for')
                       for item in page['buttons']), page
        page = browser.action(page, 'Close')
        browser.same_document(page, document)
    print('PASS public Bootstrap form modal: dynamic content, text/textarea fill, close and reopen', flush=True)

    page = browser.run('click', launchers['Open first modal'])
    assert 'Modal 1' in page['text'], page
    page = browser.action(page, 'Open second modal')
    assert 'Modal 2' in page['text'] and 'Modal 1' not in page['text'], page
    page = browser.action(page, 'Back to first')
    assert 'Modal 1' in page['text'] and 'Modal 2' not in page['text'], page
    page = browser.action(page, 'Outside dialog')
    assert not any(item['name'] == 'Outside dialog' for item in all_buttons(browser, page)), page
    browser.same_document(page, document)
    print('PASS public Bootstrap modal transitions: first, second, back, outside close', flush=True)
