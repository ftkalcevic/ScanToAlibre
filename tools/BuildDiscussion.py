# BuildDiscussion.py : writes Discussion.md, the conversation between Frank and
# Claude about ScanToAlibre word for word, from Claude Code's saved transcripts.
#
#   python tools\BuildDiscussion.py
#
# ScanToAlibre began in a session held in CAMForAlibre's folder, so this reads
# that project's transcripts from Frank's first ScanToAlibre message on (the
# one starting FIRST_MESSAGE), and all of ScanToAlibre's own, in
# %USERPROFILE%\.claude\projects\D--Projects-alibre-ScanToAlibre (sessions
# opened in this folder). Copied from CAMForAlibre's tools\BuildDiscussion.py.
# Only the chat is kept: Frank's messages, Claude's replies, Claude's questions
# with Frank's answers, and plans with whether Frank approved them. Tool calls,
# file contents and command output are left out.

import datetime
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROJECTS = os.path.join(os.path.expanduser('~'), '.claude', 'projects')
CAM_FOLDER = os.path.join(PROJECTS, 'D--Projects-alibre-AlibreCAM')
OWN_FOLDER = os.path.join(PROJECTS, 'D--Projects-alibre-ScanToAlibre')
FIRST_MESSAGE = 'Next project will be tools to help covert a 3d scan into a 3d alibre part'


def first_message_time(entries):
    """The timestamp of Frank's first ScanToAlibre message, or None."""
    for entry in entries:
        if entry.get('type') != 'user':
            continue
        content = (entry.get('message') or {}).get('content')
        texts = [content] if isinstance(content, str) else \
            [b.get('text', '') for b in content or [] if isinstance(b, dict) and b.get('type') == 'text']
        if any(FIRST_MESSAGE in t for t in texts):
            return entry['timestamp']
    return None
LOCAL = datetime.timezone(datetime.timedelta(hours=11))   # Frank's time (AEDT)


def local_time(stamp):
    when = datetime.datetime.fromisoformat(stamp.replace('Z', '+00:00'))
    return when.astimezone(LOCAL)


def clean(text):
    text = re.sub(r'<system-reminder>.*?</system-reminder>', '', text, flags=re.S)
    text = re.sub(r'</?pasted_content[^>]*>', '', text)
    text = re.sub(r'<artifact-content-authored-by-others/>', '', text)
    return text.strip()


def quote(text):
    return '\n'.join(('> ' + line) if line else '>' for line in text.split('\n'))


def result_text(block):
    content = block.get('content')
    if isinstance(content, str):
        return content
    return '\n'.join(part.get('text', '') for part in content or [] if isinstance(part, dict))


def read_entries(folder):
    entries = {}
    files = sorted(glob.glob(os.path.join(folder, '*.jsonl')), key=os.path.getsize, reverse=True)
    for rank, path in enumerate(files):
        for line in open(path, encoding='utf-8'):
            try:
                entry = json.loads(line)
            except ValueError:
                continue
            if entry.get('type') not in ('user', 'assistant') or entry.get('isSidechain'):
                continue
            if not entry.get('uuid') or not entry.get('timestamp'):
                continue
            if entry['uuid'] not in entries:
                entry['_side'] = rank > 0      # only in a smaller, forked transcript
                entries[entry['uuid']] = entry
    return sorted(entries.values(), key=lambda e: e['timestamp'])


def build(entries):
    tool_uses = {}          # id -> (name, input)
    done_results = set()
    items = []              # (time, speaker, text, side)

    for entry in entries:
        message = entry.get('message') or {}
        content = message.get('content')
        when = local_time(entry['timestamp'])
        side = entry['_side']

        if entry['type'] == 'user':
            if entry.get('isMeta') or entry.get('isCompactSummary'):
                continue
            if isinstance(content, str):
                content = [{'type': 'text', 'text': content}]
            parts = []
            for block in content or []:
                if not isinstance(block, dict):
                    continue
                kind = block.get('type')
                if kind == 'text':
                    text = clean(block.get('text', ''))
                    if text.startswith('[Request interrupted'):
                        items.append((when, 'note', 'Frank stopped Claude here.', side))
                    elif text and not text.startswith('<command-') and not text.startswith('Caveat:'):
                        parts.append(text)
                elif kind == 'image':
                    parts.append('*[picture]*')
                elif kind == 'tool_result':
                    use_id = block.get('tool_use_id')
                    if use_id not in tool_uses or use_id in done_results:
                        continue
                    done_results.add(use_id)
                    name, _ = tool_uses[use_id]
                    text = result_text(block)
                    if name == 'AskUserQuestion':
                        answers = re.findall(r'"((?:[^"\\]|\\.)*?)"="((?:[^"\\]|\\.)*?)"', text, flags=re.S)
                        if answers:
                            lines = ['Answers:'] + ['- ' + q.strip() + '\n  **' + a.strip() + '**' for q, a in answers]
                            items.append((when, 'frank', '\n'.join(lines), side))
                        elif "doesn't want to proceed" in text:
                            items.append((when, 'note', 'Frank closed the questions without answering.', side))
                    elif name == 'ExitPlanMode':
                        if text.startswith('User has approved'):
                            items.append((when, 'note', 'Frank approved the plan.', side))
                        else:
                            said = re.search(r'the user said:\s*(.*)', text, flags=re.S)
                            note = 'Frank turned the plan down.'
                            items.append((when, 'note', note, side))
                            if said:
                                items.append((when, 'frank', clean(said.group(1)), side))
            if parts:
                items.append((when, 'frank', '\n\n'.join(parts), side))
        else:
            for block in content or []:
                if not isinstance(block, dict):
                    continue
                kind = block.get('type')
                if kind == 'text':
                    text = clean(block.get('text', ''))
                    if text and text != 'No response requested.':
                        items.append((when, 'claude', text, side))
                elif kind == 'tool_use':
                    name = block.get('name')
                    data = block.get('input') or {}
                    tool_uses[block.get('id')] = (name, data)
                    if name == 'AskUserQuestion':
                        lines = ['Questions:']
                        for question in data.get('questions', []):
                            lines.append('- ' + question.get('question', ''))
                            for option in question.get('options', []):
                                lines.append('  - ' + option.get('label', '') +
                                             (': ' + option['description'] if option.get('description') else ''))
                        items.append((when, 'claude', '\n'.join(lines), side))
                    elif name == 'ExitPlanMode' and data.get('plan'):
                        items.append((when, 'claude', 'Plan:\n\n' + data['plan'].strip(), side))

    return items


def write(items, path):
    out = ['# Discussion', '',
           'The conversation between Frank and Claude, word for word, built from Claude Code\'s transcripts by '
           '`tools\\BuildDiscussion.py`. Times are Frank\'s (AEDT). Only the chat is here: Claude\'s tool calls, '
           'file edits and command output are left out. Pictures Frank pasted show as *[picture]*.',
           '',
           'ScanToAlibre began in a session held in CAMForAlibre\'s folder; it starts here at Frank\'s first message '
           'about it.', '',
           'Decisions are also recorded in the spec (`docs\\SPEC.md`) and `CLAUDE.md`.', '']
    main = [i for i in items if not i[3]]
    side = [i for i in items if i[3]]

    def emit(group):
        day = None
        last = None
        for when, speaker, text, _ in group:
            if when.date() != day:
                day = when.date()
                out.extend(['## ' + day.isoformat(), ''])
                last = None
            if speaker == 'note':
                out.extend(['*(' + text + ')*', ''])
                last = None
                continue
            if speaker != last:
                heading = ('### \U0001F9D1 Frank · ' + when.strftime('%H:%M')) if speaker == 'frank' \
                    else '### \U0001F916 Claude'
                out.extend([heading, ''])
                last = speaker
            out.extend([quote(text), ''])

    emit(main)
    if side:
        out.extend(['---', '', '# Side conversation', '',
                    'A forked copy of the session, kept apart from the main one.', ''])
        emit(side)
    with open(path, 'w', encoding='utf-8', newline='\r\n') as f:
        f.write('\n'.join(out).rstrip('\n') + '\n')


if __name__ == '__main__':
    entries = []
    if os.path.isdir(CAM_FOLDER):
        cam = read_entries(CAM_FOLDER)
        start = first_message_time(cam)
        if start is not None:
            entries.extend(e for e in cam if e['timestamp'] >= start)
    if os.path.isdir(OWN_FOLDER):
        seen = {e['uuid'] for e in entries}
        entries.extend(e for e in read_entries(OWN_FOLDER) if e['uuid'] not in seen)
    entries.sort(key=lambda e: e['timestamp'])
    # CAMForAlibre's largest transcript decides what's a fork there; ScanToAlibre's
    # session isn't it, so all of this is the main conversation.
    for e in entries:
        e['_side'] = False
    items = build(entries)
    write(items, os.path.join(ROOT, 'Discussion.md'))
    print(len(items), 'entries written')
