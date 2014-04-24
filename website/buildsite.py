#!/usr/bin/python
#
#  Build Duktape website.  Must be run with cwd in the website/ directory.
#

import os
import sys
import time
import datetime
import shutil
import re
import tempfile
import atexit
import md5
from bs4 import BeautifulSoup, Tag

colorize = True
fancy_stack = True
remove_fixme = True
testcase_refs = False
list_tags = False
fancy_releaselog = True

dt_now = datetime.datetime.utcnow()

def readFile(x):
	f = open(x, 'rb')
	data = f.read()
	f.close()
	return data

def htmlEscape(x):
	res = ''
	esc = '&<>'
	for c in x:
		if ord(c) >= 0x20 and ord(c) <= 0x7e and c not in esc:
			res += c
		else:
			res += '&#x%04x;' % ord(c)
	return res

def getAutodeleteTempname():
	tmp = tempfile.mktemp(suffix='duktape-website')
	def f():
		os.remove(tmp)
	atexit.register(f)
	return tmp

# also escapes text automatically
def sourceHighlight(x, sourceLang):
	tmp1 = getAutodeleteTempname()
	tmp2 = getAutodeleteTempname()

	f = open(tmp1, 'wb')  # FIXME
	f.write(x)
	f.close()

	# FIXME: safer execution
	os.system('source-highlight -s %s -c highlight.css --no-doc <"%s" >"%s"' % \
	          (sourceLang, tmp1, tmp2))

	f = open(tmp2, 'rb')
	res = f.read()
	f.close()

	return res

def rst2Html(filename):
	tmp1 = getAutodeleteTempname()

	# FIXME: safer execution
	os.system('rst2html "%s" >"%s"' % \
	          (filename, tmp1))

	f = open(tmp1, 'rb')
	res = f.read()
	f.close()

	return res

def getFileMd5(filename):
	if not os.path.exists(filename):
		return None
	f = open(filename, 'rb')
	d = f.read()
	f.close()
	return md5.md5(d).digest().encode('hex')

def stripNewline(x):
	if len(x) > 0 and x[-1] == '\n':
		return x[:-1]
	return x

def validateAndParseHtml(data):
	# first parse as xml to get errors out
	ign_soup = BeautifulSoup(data, 'xml')

	# then parse as lenient html, no xml tags etc
	soup = BeautifulSoup(data)

	return soup

re_stack_line = re.compile(r'^(\[[^\x5d]+\])(?:\s+->\s+(\[[^\x5d]+\]))?(?:\s+(.*?))?\s*$')
def renderFancyStack(inp_line):
	# Support various notations here:
	#
	#   [ a b c ]
	#   [ a b c ] -> [ d e f ]
	#   [ a b c ] -> [ d e f ]  (if foo)
	#

	m = re_stack_line.match(inp_line)
	#print(inp_line)
	assert(m is not None)
	stacks = [ m.group(1) ]
	if m.group(2) is not None:
		stacks.append(m.group(2))

	res = []

	res.append('<div class="stack-wrapper">')
	for idx, stk in enumerate(stacks):
		if idx > 0:
			res.append('<span class="arrow"><b>&rarr;</b></span>')
		res.append('<span class="stack">')
		for part in stk.split(' '):
			part = part.strip()
			elem_classes = []
			elem_classes.append('elem')  #FIXME
			if len(part) > 0 and part[-1] == '!':
				part = part[:-1]
				elem_classes.append('active')
			elif len(part) > 0 and part[-1] == '*':
				part = part[:-1]
				elem_classes.append('referred')
			elif len(part) > 0 and part[-1] == '?':
				part = part[:-1]
				elem_classes.append('ghost')

			text = part

			# FIXME: detect special constants like "true", "null", etc?
			if text in [ 'undefined', 'null', 'true', 'false', 'NaN' ] or \
			   (len(text) > 0 and text[0] == '"' and text[-1] == '"'):
				elem_classes.append('literal')

			# FIXME: inline elements for reduced size?
			# The stack elements use a classless markup to minimize result
			# HTML size.  HTML inline elements are used to denote different
			# kinds of elements; the elements should be reasonable for text
			# browsers so a limited set can be used.
			use_inline = False

			if part == '':
				continue
			if part == '[':
				#res.append('<em>[</em>')
				res.append('<span class="cap">[</span>')
				continue
			if part == ']':
				#res.append('<em>]</em>')
				res.append('<span class="cap">]</span>')
				continue

			if part == '...':
				text = '. . .'
				elem_classes.append('ellipsis')
			else:
				text = part

			if 'ellipsis' in elem_classes and use_inline:
				res.append('<i>' + htmlEscape(text) + '</i>')
			elif 'active' in elem_classes and use_inline:
				res.append('<b>' + htmlEscape(text) + '</b>')
			else:
				res.append('<span class="' + ' '.join(elem_classes) + '">' + htmlEscape(text) + '</span>')

		res.append('</span>')

	# FIXME: pretty badly styled now
	if m.group(3) is not None:
		res.append('<span class="stack-comment">' + htmlEscape(m.group(3)) + '</span>')

	res.append('</div>')

	return ' '.join(res) + '\n'  # stack is a one-liner; spaces are for text browser rendering

def parseApiDoc(filename):
	f = open(filename, 'rb')
	parts = {}
	state = None
	for line in f.readlines():
		line = stripNewline(line)
		if line.startswith('='):
			state = line[1:]
		elif state is not None:
			if not parts.has_key(state):
				parts[state] = []
			parts[state].append(line)
		else:
			if line != '':
				raise Exception('unparsed non-empty line: %r' % line)
			else:
				# ignore
				pass
	f.close()

	# remove leading and trailing empty lines
	for k in parts:
		p = parts[k]
		while len(p) > 0 and p[0] == '':
			p.pop(0)
		while len(p) > 0 and p[-1] == '':
			p.pop()

	return parts

def processApiDoc(parts, funcname, testrefs, used_tags):
	res = []

	# the 'hidechar' span is to allow browser search without showing the char
	res.append('<h1 id="%s"><a href="#%s"><span class="hidechar">.</span>%s()</a></h1>' % (funcname, funcname, funcname))

	if parts.has_key('proto'):
		p = parts['proto']
		res.append('<h2>Prototype</h2>')
		res.append('<pre class="c-code">')
		for i in p:
			res.append(htmlEscape(i))
		res.append('</pre>')
		res.append('')
	else:
		pass

	if parts.has_key('stack'):
		p = parts['stack']
		res.append('<h2>Stack</h2>')
		for line in p:
			res.append('<pre class="stack">' + \
			           '%s' % htmlEscape(line) + \
			           '</pre>')
		res.append('')
	else:
		res.append('<h2>Stack</h2>')
		res.append('<p>No effect.</p>')
		res.append('')

	if parts.has_key('summary'):
		p = parts['summary']
		res.append('<h2>Summary</h2>')

		# If text contains a '<p>', assume it is raw HTML; otherwise
		# assume it is a single paragraph (with no markup) and generate
		# paragraph tags, escaping into HTML

		raw_html = False
		for i in p:
			if '<p>' in i:
				raw_html = True

		if raw_html:
			for i in p:
				res.append(i)
		else:
			res.append('<p>')
			for i in p:
				res.append(htmlEscape(i))
			res.append('</p>')
		res.append('')

	if parts.has_key('example'):
		p = parts['example']
		res.append('<h2>Example</h2>')
		res.append('<pre class="c-code">')
		for i in p:
			res.append(htmlEscape(i))
		res.append('</pre>')
		res.append('')

	if parts.has_key('seealso'):
		p = parts['seealso']
		res.append('<h2>See also</h2>')
		res.append('<ul>')
		for i in p:
			res.append('<li><a href="#%s">%s</a></li>' % (htmlEscape(i), htmlEscape(i)))
		res.append('</ul>')

	if testcase_refs:
		res.append('<h2>Related test cases</h2>')
		if testrefs.has_key(funcname):
			res.append('<ul>')
			for i in testrefs[funcname]:
				res.append('<li>%s</li>' % htmlEscape(i))
			res.append('</ul>')
		else:
			res.append('<p>None.</p>')

	if not testrefs.has_key(funcname):
		res.append('<div class="fixme">This API call has no test cases.</div>')
		
	if list_tags and parts.has_key('tags'):
		# FIXME: placeholder
		res.append('<h2>Tags</h2>')
		res.append('<p>')
		p = parts['tags']
		for idx, val in enumerate(p):
			if idx > 0:
				res.append(' ')
			res.append(htmlEscape(val))
		res.append('</p>')
		res.append('')

	if parts.has_key('fixme'):
		p = parts['fixme']
		res.append('<div class="fixme">')
		for i in p:
			res.append(htmlEscape(i))
		res.append('</div>')
		res.append('')

	return res

def processRawDoc(filename):
	f = open(filename, 'rb')
	res = []
	for line in f.readlines():
		line = stripNewline(line)
		res.append(line)
	f.close()
	res.append('')
	return res

def transformColorizeCode(soup, cssClass, sourceLang):
	for elem in soup.select('pre.' + cssClass):
		input_str = elem.string
		if len(input_str) > 0 and input_str[0] == '\n':
			# hack for leading empty line
			input_str = input_str[1:]

		colorized = sourceHighlight(input_str, sourceLang)

		# source-highlight generates <pre><tt>...</tt></pre>, get rid of <tt>
		new_elem = BeautifulSoup(colorized).tt    # XXX: parse just a fragment - how?
		new_elem.name = 'pre'
		new_elem['class'] = cssClass

		elem.replace_with(new_elem)

def transformFancyStacks(soup):
	for elem in soup.select('pre.stack'):
		input_str = elem.string
		if len(input_str) > 0 and input_str[0] == '\n':
			# hack for leading empty line
			input_str = input_str[1:]

		new_elem = BeautifulSoup(renderFancyStack(input_str)).div  # XXX: fragment?
		elem.replace_with(new_elem)

def transformRemoveClass(soup, cssClass):
	for elem in soup.select('.' + cssClass):
		elem.extract()

def transformReadIncludes(soup, includeDir):
	for elem in soup.select('pre'):
		if not elem.has_key('include'):
			continue
		filename = elem['include']
		del elem['include']
		f = open(os.path.join(includeDir, filename), 'rb')
		elem.string = f.read()
		f.close()

def transformVersionNumber(soup, verstr):
	for elem in soup.select('.duktape-version'):
		elem.replaceWith(verstr)

def transformCurrentDate(soup):
	curr_date = '%04d-%02d-%02d' % (dt_now.year, dt_now.month, dt_now.day)
	for elem in soup.select('.current-date'):
		elem.replaceWith(curr_date)

def transformAddHrBeforeH1(soup):
	for elem in soup.select('h1'):
		elem.insert_before(soup.new_tag('hr'))

# Add automatic anchors so that a basename from an element with an explicit
# ID is appended with dotted number(s).  Note that headings do not actually
# nest in the document, so this is now based on document order traversal and
# keeping track of counts of headings at different levels, and the active
# explicit IDs at each level.
def transformAddAutoAnchorsNumbered(soup):
	level_counts = [ 0, 0, 0, 0, 0, 0 ]                    # h1, h2, h3, h4, h5, h6
	level_ids = [ None, None, None, None, None, None ]     # explicit IDs
	hdr_tags = { 'h1': 0, 'h2': 1, 'h3': 2, 'h4': 3, 'h5': 4, 'h6': 5 }

	changes = []

	def _proc(root, state):
		idx = hdr_tags.get(root.name, None)
		if idx is None:
			return

		# bump count at matching level and zero lower levels
		level_counts[idx] += 1
		for i in xrange(idx + 1, 6):
			level_counts[i] = 0

		# set explicit ID for current level
		if root.has_key('id'):
			level_ids[idx] = root['id']
			return

		# no explicit ID at current level, clear it
		level_ids[idx] = None

		# figure out an automatic ID: closest explicit ID + dotted
		# numbers to current level

		parts = []
		for i in xrange(idx, -1, -1):  # idx, idx-1, ..., 0
			if level_ids[i] is not None:
				parts.append(level_ids[i])
				break
			parts.append(str(level_counts[i]))
			if i == 0:
				parts.append('doc')  # if no ID in path, use e.g. 'doc.1.2'
		parts.reverse()
		auto_id = '.'.join(parts)

		# avoid mutation: record changes to be made first
		# (adding 'id' would be OK, but this is more flexible
		# if explicit anchors are added instead / in addition
		# to 'id' attributes)
		changes.append((root, auto_id))

	def _rec(root, state):
		if not isinstance(root, Tag):
			return
		_proc(root, state)
		for elem in root.children:
			_rec(elem, state)

	_rec(soup.select('body')[0], {})

	for elem, auto_id in changes:
		elem['id'] = auto_id

# Add automatic anchors where section headings are used to autogenerate
# suitable names.  This does not work very well: there are many subsections
# with the name "Example" or "Limitations", for instance.  Prepending the
# parent name (or rather names of all the parents) would create very long
# names.
def transformAddAutoAnchorsNamed(soup):
	hdr_tags = [ 'h1', 'h2', 'h3', 'h4', 'h5', 'h6' ]

	ids = {}

	def findAutoName(txt):
		# simple name sanitation, not very well thought out; goal is to get
		# nice web-like anchor names from whatever titles are present
		txt = txt.strip().lower()
		if len(txt) > 1 and txt[0] == '.':
			txt = txt[1:]  # leading dot convention for API section names
		txt = txt.replace('c++', 'cpp')
		txt = txt.replace('. ', ' ')  # e.g. 'vs.' -> 'vs'
		txt = txt.replace(', ', ' ')  # e.g. 'foo, bar' -> 'foo bar'
		txt = txt.replace(' ', '_')
		res = ''
		for i,c in enumerate(txt):
			if (ord(c) >= ord('a') and ord(c) <= ord('z')) or \
			   (ord(c) >= ord('A') and ord(c) <= ord('Z')) or \
			   (ord(c) >= ord('0') and ord(c) <= ord('9') and i > 0) or \
			   c in '_':
				res += c
			elif c in '()[]{}?\'"':
				pass  # eat
			else:
				res += '_'
		return res

	for elem in soup.select('*'):
		if not elem.has_key('id'):
			continue
		e_id = elem['id']
		if ids.has_key(e_id):
			print('WARNING: duplicate id %s' % e_id)
		ids[e_id] = True

	# add automatic anchors for every other heading, with priority in
	# naming for higher level sections (e.g. h2 over h3)
	for hdr in hdr_tags:
		for elem in soup.select(hdr):
			if elem.has_key('id'):
				continue  # already has an id anchor
			e_name = elem.text
			a_name = findAutoName(e_name)
			if ids.has_key(a_name):
				print('WARNING: cannot generate automatic anchor name for %s (already exists)' % e_name)
				continue
			ids[a_name] = True
			elem['id'] = a_name

def transformAddHeadingLinks(soup):
	hdr_tags = [ 'h1', 'h2', 'h3', 'h4', 'h5', 'h6' ]
	changes = []

	for elem in soup.select('*'):
		if elem.name not in hdr_tags or not elem.has_key('id'):
			continue

		new_elem = soup.new_tag('a')
		new_elem['href'] = '#' + elem['id']
		new_elem['class'] = 'sectionlink'
		new_elem.string = u'\u00a7'  # section sign

		# avoid mutation while iterating
		changes.append((elem, new_elem))

	for elem, new_elem in changes:
		if elem.has_key('class'):
			elem['class'] = elem['class'] + ' sectiontitle'
		else:
			elem['class'] = 'sectiontitle'
		elem.append(' ')
		elem.append(new_elem)

def setNavSelected(soup, pagename):
	# pagename must match <li><a> content
	for elem in soup.select('#site-top-nav li'):
		if elem.text == pagename:
			elem['class'] = 'selected'

# FIXME: refactor shared parts

def scanApiCalls(apitestdir):	
	re_api_call = re.compile(r'duk_[0-9a-zA-Z_]+')

	res = {}  # api call -> [ test1, ..., testN ]

	tmpfiles = os.listdir(apitestdir)
	for filename in tmpfiles:
		if os.path.splitext(filename)[1] != '.c':
			continue

		f = open(os.path.join(apitestdir, filename))
		data = f.read()
		f.close()

		apicalls = re_api_call.findall(data)
		for i in apicalls:
			if not res.has_key(i):
				res[i] = []
			if filename not in res[i]:
				res[i].append(filename)

	for k in res.keys():
		res[k].sort()

	return res

def createTagIndex(api_docs, used_tags):
	res = []
	res.append('<h1 id="bytag">API calls by tag</h1>')

	for tag in used_tags:
		res.append('<h2>' + htmlEscape(tag) + '</h2>')
		res.append('<ul class="taglist">')
		for doc in api_docs:
			if not doc['parts'].has_key('tags'):
				continue
			for i in doc['parts']['tags']:
				if i != tag:
					continue
				res.append('<li><a href="#%s">%s</a></li>' % (htmlEscape(doc['name']), htmlEscape(doc['name'])))
		res.append('</ul>')

	return res

def generateApiDoc(apidocdir, apitestdir):
	templ_soup = validateAndParseHtml(readFile('template.html'))
	setNavSelected(templ_soup, 'API')

	# scan api files

	tmpfiles = os.listdir(apidocdir)
	apifiles = []
	for filename in tmpfiles:
		if os.path.splitext(filename)[1] == '.txt':
			apifiles.append(filename)
	apifiles.sort()
	#print(apifiles)
	print '%d api files' % len(apifiles)

	# scan api testcases for references to API calls

	testrefs = scanApiCalls(apitestdir)
	#print(repr(testrefs))

	# title

	title_elem = templ_soup.select('#template-title')[0]
	del title_elem['id']
	title_elem.string = 'Duktape API'

	# scan api doc files

	used_tags = []
	api_docs = []   # [ { 'parts': xxx, 'name': xxx } ]

	for filename in apifiles:
		parts = parseApiDoc(os.path.join(apidocdir, filename))
		funcname = os.path.splitext(os.path.basename(filename))[0]
		if parts.has_key('tags') and 'omit' in parts['tags']:
			print 'Omit API doc: ' + str(funcname)
			continue
		if parts.has_key('tags'):
			for i in parts['tags']:
				if i not in used_tags:
					used_tags.append(i)
		api_docs.append({ 'parts': parts, 'name': funcname })

	used_tags.sort()

	# nav

	res = []
	navlinks = []
	navlinks.append(['#introduction', 'Introduction'])
	navlinks.append(['#notation', 'Notation'])
	navlinks.append(['#concepts', 'Concepts'])
	navlinks.append(['#defines', 'Header definitions'])
	navlinks.append(['#bytag', 'API calls by tag'])
	navlinks.append(['', u'\u00a0'])  # XXX: force vertical space
	for doc in api_docs:
		funcname = doc['name']
		navlinks.append(['#' + funcname, funcname])
	res.append('<ul>')
	for nav in navlinks:
		res.append('<li><a href="' + htmlEscape(nav[0]) + '">' + htmlEscape(nav[1]) + '</a></li>')
	res.append('</ul>')

	nav_soup = validateAndParseHtml('\n'.join(res))
	tmp_soup = templ_soup.select('#site-middle-nav')[0]
	tmp_soup.clear()
	for i in nav_soup.select('body')[0]:
		tmp_soup.append(i)

	# content

	res = []
	res += [ '<div class="main-title"><strong>Duktape API</strong></div>' ]

	# FIXME: generate from the same list as nav links for these
	res += processRawDoc('api/intro.html')
	res += processRawDoc('api/notation.html')
	res += processRawDoc('api/concepts.html')
	res += processRawDoc('api/defines.html')

	# tag index
	res += createTagIndex(api_docs, used_tags)

	# api docs
	for doc in api_docs:
		# FIXME: Here we'd like to validate individual processApiDoc() results so
		# that they don't e.g. have unbalanced tags.  Or at least normalize them so
		# that they don't break the entire page.

		try:
			data = processApiDoc(doc['parts'], doc['name'], testrefs, used_tags)
			res += data
		except:
			print repr(data)
			print 'FAIL: ' + repr(filename)
			raise

	print('used tags: ' + repr(used_tags))

	content_soup = validateAndParseHtml('\n'.join(res))
	tmp_soup = templ_soup.select('#site-middle-content')[0]
	tmp_soup.clear()
	for i in content_soup.select('body')[0]:
		tmp_soup.append(i)
	tmp_soup['class'] = 'content'

	return templ_soup

def generateIndexPage():
	templ_soup = validateAndParseHtml(readFile('template.html'))
	index_soup = validateAndParseHtml(readFile('index/index.html'))
	setNavSelected(templ_soup, 'Home')

	title_elem = templ_soup.select('#template-title')[0]
	del title_elem['id']
	title_elem.string = 'Duktape'

	tmp_soup = templ_soup.select('#site-middle')[0]
	tmp_soup.clear()
	for i in index_soup.select('body')[0]:
		tmp_soup.append(i)
	tmp_soup['class'] = 'content'

	return templ_soup

def generateDownloadPage(releases_filename):
	templ_soup = validateAndParseHtml(readFile('template.html'))
	down_soup = validateAndParseHtml(readFile('download/download.html'))
	setNavSelected(templ_soup, 'Download')

	title_elem = templ_soup.select('#template-title')[0]
	del title_elem['id']
	title_elem.string = 'Downloads'

	if fancy_releaselog:
		# fancy releaselog
		rel_data = rst2Html(os.path.abspath(os.path.join('..', 'RELEASES.txt')))
		rel_soup = BeautifulSoup(rel_data)
		released = rel_soup.select('#released')[0]
		# massage the rst2html generated HTML to be more suitable
		for elem in released.select('h1'):
			elem.extract()
		releaselog_elem = down_soup.select('#releaselog')[0]
		releaselog_elem.insert_after(released)
	else:
		# plaintext releaselog
		releaselog_elem = down_soup.select('#releaselog')[0]
		pre_elem = down_soup.new_tag('pre')
		releaselog_elem.append(pre_elem)
		f = open(releases_filename, 'rb')
		pre_elem.string = f.read().decode('utf-8')
		f.close()

	# automatic md5sums for downloadable files
	# <tr><td class="reldate">2013-09-21</td>
	#     <td class="filename"><a href="duktape-0.6.0.tar.xz">duktape-0.6.0.tar.xz</a></td>
	#     <td class="description">alpha, first round of work on public API</td>
	#     <td class="hash">fa384a42a27d996313e0192c51c50b4a</td></tr>

	for tr in down_soup.select('tr'):
		tmp = tr.select('.filename')
		if len(tmp) != 1:
			continue
		href = tmp[0].select('a')[0]['href']
		hash_elem = tr.select('.hash')[0]
		hash_elem.string = getFileMd5(os.path.abspath(os.path.join('binaries', href))) or '???'

	tmp_soup = templ_soup.select('#site-middle')[0]
	tmp_soup.clear()
	for i in down_soup.select('body')[0]:
		tmp_soup.append(i)
	tmp_soup['class'] = 'content'

	return templ_soup

def generateGuide():
	templ_soup = validateAndParseHtml(readFile('template.html'))
	setNavSelected(templ_soup, 'Guide')

	title_elem = templ_soup.select('#template-title')[0]
	del title_elem['id']
	title_elem.string = 'Duktape Programmer\'s Guide'

	# nav

	res = []
	navlinks = []
	navlinks.append(['#introduction', 'Introduction'])
	navlinks.append(['#gettingstarted', 'Getting started'])
	navlinks.append(['#programming', 'Programming model'])
	navlinks.append(['#types', 'Stack types'])
	navlinks.append(['#typealgorithms', 'Type algorithms'])
	navlinks.append(['#duktapebuiltins', 'Duktape built-ins'])
	navlinks.append(['#es6features', 'Ecmascript E6 features'])
	navlinks.append(['#custombehavior', 'Custom behavior'])
	navlinks.append(['#customjson', 'Custom JSON formats'])
	navlinks.append(['#errorobjects', 'Error objects'])
	navlinks.append(['#functionobjects', 'Function objects'])
	navlinks.append(['#finalization', 'Finalization'])
	navlinks.append(['#coroutines', 'Coroutines'])
	navlinks.append(['#propertyvirtualization', 'Property virtualization'])
	navlinks.append(['#compiling', 'Compiling'])
	navlinks.append(['#performance', 'Performance'])
	navlinks.append(['#portability', 'Portability'])
	navlinks.append(['#compatibility', 'Compatibility'])
	navlinks.append(['#limitations', 'Limitations'])
	navlinks.append(['#comparisontolua', 'Comparison to Lua'])
	res.append('<ul>')
	for nav in navlinks:
		res.append('<li><a href="' + htmlEscape(nav[0]) + '">' + htmlEscape(nav[1]) + '</a></li>')
	res.append('</ul>')

	nav_soup = validateAndParseHtml('\n'.join(res))
	tmp_soup = templ_soup.select('#site-middle-nav')[0]
	tmp_soup.clear()
	for i in nav_soup.select('body')[0]:
		tmp_soup.append(i)

	# content

	res = []
	res += [ '<div class="main-title"><strong>Duktape Programmer\'s Guide</strong></div>' ]

	res += processRawDoc('guide/intro.html')
	res += processRawDoc('guide/gettingstarted.html')
	res += processRawDoc('guide/programming.html')
	res += processRawDoc('guide/stacktypes.html')
	res += processRawDoc('guide/typealgorithms.html')
	res += processRawDoc('guide/duktapebuiltins.html')
	res += processRawDoc('guide/es6features.html')
	res += processRawDoc('guide/custombehavior.html')
	res += processRawDoc('guide/customjson.html')
	res += processRawDoc('guide/errorobjects.html')
	res += processRawDoc('guide/functionobjects.html')
	res += processRawDoc('guide/finalization.html')
	res += processRawDoc('guide/coroutines.html')
	res += processRawDoc('guide/propertyvirtualization.html')
	res += processRawDoc('guide/compiling.html')
	res += processRawDoc('guide/performance.html')
	res += processRawDoc('guide/portability.html')
	res += processRawDoc('guide/compatibility.html')
	res += processRawDoc('guide/limitations.html')
	res += processRawDoc('guide/luacomparison.html')

	content_soup = validateAndParseHtml('\n'.join(res))
	tmp_soup = templ_soup.select('#site-middle-content')[0]
	tmp_soup.clear()
	for i in content_soup.select('body')[0]:
		tmp_soup.append(i)
	tmp_soup['class'] = 'content'

	return templ_soup

def generateStyleCss():
	styles = [
		'reset.css',
		'style-html.css',
		'style-content.css',
		'style-top.css',
		'style-middle.css',
		'style-bottom.css',
		'style-index.css',
		'style-download.css',
		'style-api.css',
		'style-guide.css',
		'highlight.css'
	]

	style = ''
	for i in styles:
		style += '/* === %s === */\n' % i
		style += readFile(i)

	return style

def postProcess(soup, includeDir, autoAnchors=False, headingLinks=False, duktapeVersion=None):
	# read in source snippets from include files
	if True:
		transformReadIncludes(soup, includeDir)

	# version number
	if True:
		transformVersionNumber(soup, duktapeVersion)

	# current date
	if True:
		transformCurrentDate(soup)

	# add <hr> elements before all <h1> elements to improve readability
	# in text browsers
	if True:
		transformAddHrBeforeH1(soup)

	# add automatic anchors to all headings (as long as they don't conflict
	# with any manually assigned "long term" ids)
	if autoAnchors:
		transformAddAutoAnchorsNumbered(soup)

	if headingLinks:
		transformAddHeadingLinks(soup)

	if colorize:
		transformColorizeCode(soup, 'c-code', 'c')
		transformColorizeCode(soup, 'ecmascript-code', 'javascript')

	if fancy_stack:
		transformFancyStacks(soup)

	if remove_fixme:
		transformRemoveClass(soup, 'fixme')

	return soup

def writeFile(name, data):
	f = open(name, 'wb')
	f.write(data)
	f.close()

def scrapeDuktapeVersion():
	f = open(os.path.join('..', 'src', 'duktape.h'))
	re_ver = re.compile(r'^#define DUK_VERSION\s+(\d+)L?\s*$')
	for line in f:
		line = line.strip()
		m = re_ver.match(line)
		if m is None:
			continue
		raw_ver = int(m.group(1))
		str_ver = '%d.%d.%d' % ( raw_ver / 10000, raw_ver / 100 % 100, raw_ver % 100)
	f.close()
	if raw_ver is None:
		raise Exception('cannot scrape Duktape version')
	return str_ver, raw_ver

def main():
	outdir = sys.argv[1]; assert(outdir)
	apidocdir = 'api'
	apitestdir = '../api-testcases'
	guideincdir = '../examples/guide'
	apiincdir = '../examples/api'
	out_charset = 'utf-8'
	releases_filename = '../RELEASES.txt'

	duk_verstr, duk_verint = scrapeDuktapeVersion()
	print 'Scraped version number: ' + duk_verstr

	print 'Generating style.css'
	data = generateStyleCss()
	writeFile(os.path.join(outdir, 'style.css'), data)
	#writeFile(os.path.join(outdir, 'reset.css'), readFile('reset.css'))
	#writeFile(os.path.join(outdir, 'highlight.css'), readFile('highlight.css'))

	print 'Generating api.html'
	soup = generateApiDoc(apidocdir, apitestdir)
	soup = postProcess(soup, apiincdir, autoAnchors=True, headingLinks=True, duktapeVersion=duk_verstr)
	writeFile(os.path.join(outdir, 'api.html'), soup.encode(out_charset))

	print 'Generating guide.html'
	soup = generateGuide()
	soup = postProcess(soup, guideincdir, autoAnchors=True, headingLinks=True, duktapeVersion=duk_verstr)
	writeFile(os.path.join(outdir, 'guide.html'), soup.encode(out_charset))

	print 'Generating index.html'
	soup = generateIndexPage()
	soup = postProcess(soup, None, duktapeVersion=duk_verstr)
	writeFile(os.path.join(outdir, 'index.html'), soup.encode(out_charset))

	print 'Generating download.html'
	soup = generateDownloadPage(releases_filename)
	soup = postProcess(soup, None, duktapeVersion=duk_verstr)
	writeFile(os.path.join(outdir, 'download.html'), soup.encode(out_charset))

	print 'Copying misc files'
	for i in [ 'favicon.ico',
	           'startup_image_320x480.png',
	           'touch_icon_114x114.png',
	           'touch_icon_120x120.png',
	           'touch_icon_144x144.png',
	           'touch_icon_152x152.png',
	           'touch_icon_57x57.png',
	           'touch_icon_60x60.png',
	           'touch_icon_72x72.png' ]:
		shutil.copyfile(os.path.join('./', i), os.path.join(outdir, i))

	print 'Copying binaries'
	for i in os.listdir('binaries'):
		shutil.copyfile(os.path.join('binaries', i), os.path.join(outdir, i))

	print 'Copying dukweb.js files'
	for i in [ '../dukweb.js',
	           '../jquery-1.11.0.js',
	           '../dukweb/dukweb.css',
	           '../dukweb/dukweb.html' ]:
		shutil.copyfile(os.path.join('./', i), os.path.join(outdir, os.path.basename(i)))

if __name__ == '__main__':
	main()

