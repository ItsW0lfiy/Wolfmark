// Exercise the actual host-injected bridge without compiling the Servo engine.
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const source = fs.readFileSync(require('node:path').join(__dirname, '../src/main.rs'), 'utf8');
const start = source.indexOf('            const editor=', source.indexOf('// Only host-supplied bridge'));
const end = source.indexOf('        }})()"#)', start);
const script = source.slice(start, end).replaceAll('{{', '{').replaceAll('}}', '}')
  .replaceAll('{selection_start}', '0').replaceAll('{selection_end}', '0').replaceAll('{scroll_top}', '0');
class Element {
  constructor() { this.listeners = {}; this.dataset = {}; this.value = 'hello'; this.selectionStart = 0; this.selectionEnd = 0; this.scrollTop = 0; this.scrollHeight = 1000; this.clientHeight = 100; this.textContent = ''; }
  addEventListener(name, fn) { (this.listeners[name] ??= []).push(fn); }
  emit(name, props = {}) { const event = {preventDefault() {this.prevented = true;}, ...props}; for (const fn of this.listeners[name] ?? []) fn(event); return event; }
  setSelectionRange(a,b) { this.selectionStart=a; this.selectionEnd=b; }
  setRangeText() { throw Error('IME shortcut must not mutate source'); }
  focus() {}
}
const editor = new Element(), rich = new Element(), mirror = new Element(), gutter = new Element();
const classes = new Set();
const context = {classList: {add: x=>classes.add(x), remove: x=>classes.delete(x), contains: x=>classes.has(x)}};
rich.closest = ()=>context;
rich.scrollHeight=1900;
const nodes = {'source-editor':editor, 'live-rich':rich, 'live-highlights':mirror, 'live-line-numbers':gutter, 'editor-status':new Element()};
vm.runInNewContext(`(()=>{${script}})()`, {document:{getElementById:id=>nodes[id]}});
rich.emit('click');
assert(classes.has('editing'));
editor.scrollTop=450;
editor.emit('scroll');
assert.equal(mirror.scrollTop,450);
assert.equal(gutter.scrollTop,450);
editor.emit('compositionstart');
for (const key of ['Escape','Tab','b']) {
  const event=editor.emit('keydown',{key, ctrlKey:key==='b', isComposing:true});
  assert(!event.prevented, 'IME must own '+key);
}
assert(classes.has('editing'));
editor.emit('compositionend');
assert.equal(editor.dataset.composing,'0');
assert.equal(editor.dataset.unsynced,'1');
editor.emit('keydown',{key:'Escape'});
assert(!classes.has('editing'));
assert.equal(rich.scrollTop,900, 'preview restores proportional reading position');
rich.scrollTop=180;
rich.emit('click');
assert.equal(editor.scrollTop,90, 'editing resumes at preview position');
console.log('Editor bridge: composition ownership, committed input, scroll mirrors and Live transition passed');
