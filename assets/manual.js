(() => {
  const dialog = document.querySelector('#search-dialog');
  const input = document.querySelector('#search-field');
  const results = document.querySelector('#search-results');
  const status = document.querySelector('#search-status');
  const open = document.querySelector('.search-open');
  const close = document.querySelector('#close-search');
  const navButton = document.querySelector('.nav-toggle');
  const nav = document.querySelector('#side-nav');

  function setMenu(expanded) {
    nav.classList.toggle('open', expanded);
    navButton.setAttribute('aria-expanded', String(expanded));
    navButton.setAttribute('aria-label', expanded ? 'Close navigation' : 'Open navigation');
  }

  navButton.addEventListener('click', () => setMenu(!nav.classList.contains('open')));
  document.addEventListener('click', event => {
    if (!nav.contains(event.target) && !navButton.contains(event.target)) setMenu(false);
  });
  function renderResults() {
    const query = input.value.trim().toLowerCase();
    const terms = query.split(/\s+/).filter(Boolean);
    const matches = window.manualIndex.map(entry => {
      const title = entry.title.toLowerCase();
      const chapter = entry.chapter.toLowerCase();
      const searchable = chapter + ' ' + title + ' ' + entry.body.toLowerCase();
      if (!terms.every(term => searchable.includes(term))) return null;
      const rank = terms.reduce((sum, term) => sum + (title.includes(term) ? 6 : 0) + (chapter.includes(term) ? 2 : 0), 0)
        + (query && title.includes(query) ? 5 : 0) + (entry.top ? 1 : 0);
      return {entry, rank};
    }).filter(Boolean).filter(match => query || match.entry.top)
      .sort((a, b) => b.rank - a.rank);
    const shown = matches.slice(0, 16);
    status.textContent = query ? (shown.length ? (matches.length > shown.length ? 'First ' + shown.length + ' of ' + matches.length + ' matches' : matches.length + (matches.length === 1 ? ' match' : ' matches')) : 'No matches. Try a control name, MIDI number or command.') : 'Browse chapters';
    results.replaceChildren();
    for (const {entry} of shown) {
      const link = document.createElement('a');
      link.href = entry.url;
      const title = document.createElement('strong');
      title.textContent = entry.title;
      const context = document.createElement('small');
      context.textContent = entry.top ? entry.group : entry.chapter;
      link.append(title, context);
      link.addEventListener('click', () => dialog.close());
      results.append(link);
    }
  }

  function openSearch() {
    setMenu(false);
    input.value = '';
    renderResults();
    if (!dialog.open) dialog.showModal();
    input.focus();
  }
  open.addEventListener('click', openSearch);
  close.addEventListener('click', () => dialog.close());
  input.addEventListener('input', renderResults);
  dialog.addEventListener('click', event => {
    if (event.target !== dialog) return;
    const rect = dialog.getBoundingClientRect();
    if (event.clientX < rect.left || event.clientX > rect.right || event.clientY < rect.top || event.clientY > rect.bottom) dialog.close();
  });
  input.addEventListener('keydown', event => {
    if (event.key === 'ArrowDown') {
      event.preventDefault();
      results.querySelector('a')?.focus();
    } else if (event.key === 'Enter') {
      results.querySelector('a')?.click();
    }
  });
  results.addEventListener('keydown', event => {
    if (event.key !== 'ArrowUp' && event.key !== 'ArrowDown') return;
    event.preventDefault();
    const next = event.key === 'ArrowUp' ? event.target.previousElementSibling : event.target.nextElementSibling;
    (next || (event.key === 'ArrowUp' ? input : event.target)).focus();
  });
  document.addEventListener('keydown', event => {
    if (event.key === 'Escape' && nav.classList.contains('open')) {
      setMenu(false);
      navButton.focus();
    }
    const typing = event.target.isContentEditable || /^(INPUT|TEXTAREA|SELECT)$/.test(event.target.tagName);
    if ((event.key === '/' && !typing && !event.metaKey && !event.ctrlKey) || ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'k')) {
      event.preventDefault();
      openSearch();
    }
  });

  if (location.hash && document.body.querySelector('.sources-index')) {
    const section = document.getElementById(location.hash.slice(1));
    section?.querySelector('details')?.setAttribute('open', '');
  }
})();
