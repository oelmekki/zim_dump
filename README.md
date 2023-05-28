# Zim_dump

Zim_dump is a tool allowing to dump content from a [zim archive](https://black-book-editions.fr/forums.php?topic_id=45921&tid=991061#msg991061), the file format used by [Kiwix](https://www.kiwix.org/en/)
to offer offline wikipedia browsing.

## Usage

```
zim_dump [-h|--help] [-m] [-a [-t <whitelisted mime-types>]] <zimfile> [url]

Parse a zimfile and print articles' urls and names on STDOUT.

If `-a` is provided, also print the content of those articles.
By default, only the mime-types starting with `text/plain` and
`text/html` are shown. You can provide a comma separated list of
whitelisted mime-types with the `-t` option. If the mime-type of the
article is not in the list, it will only print `NOT-WHITELISTED-MIME-TYPE`.

If `-m` is provided, print instead the list of mime-types in the archive,
ignoring other options.

If `url` is provided, print instead the content of the article corresponding to the
provided url. Those urls are the ones provided while listing all articles.
In that case, options are ignored.
```

## Why?

I've been using this for a few years to build a local search engine : I
have dumps of the zimfiles for wikipedia (english and french), wikisource
(english and french), wiktionary (you've guessed it), stackoverflow, and a
few thematic wikis. With zim_dump, I extract title and url for all articles
from all those archives, store them in a sqlite database, and then I can
use its full text search feature to (kind of) quickly find an article whose
name match a query of mine. That means I can perform most of my search
without even issuing a request through the internet. This is cool.

But lately, I realized that this tool could also be useful to people
getting into machine learning. With zim_dump, you can use [the vast amount
of compressed data offered by kiwix](https://wiki.kiwix.org/wiki/Content_in_all_languages)
to train your models. And since you're working with a compressed archive,
it's optimized for size. I've modified zim_dump to add the `-a` flag, which
allows to dump not just the title and url of the article, but also its
content. Now, you can stream all wikipedia articles to you model training,
and it only takes 46gb on your hard drive (that's the size of
wikipedia_en_all_nopic_2021-11.zim, which I'm using at the time of writing).


## Installation

There are four dependencies:

* a gcc compatible compiler (default to gcc)
* pkg-config
* liblzma (with `liblzma-dev` on debian-like systems)
* libzstd (with `libzstd-dev` on debian-like systems)

To build and install :

```
make
sudo make install
```

This will install in `/usr/local/bin/` by default. You can choose an other
location while installing :

```
make install PREFIX=/home/foo/bin
```


## Output

When streaming all the content of the archive, it will be formatted like
this:

```
<START_OF_ZIM_ARTICLE>
url: /foo/bar.html
title: Foo Bar
mime-type: text/html
content:
<html>
<body>
<p>Foo.</p>
<p>Bar.</p>
</body>
</html>
<END_OF_ZIM_ARTICLE>
...
```

Content is only provided if the article is one of the whitelisted
mime-types (by default : text/plain and text/html). Otherwise, it will
print "NOT-WHITELISTED-MIME-TYPE".

Some article will have no mime-type nor content, because they are
redirects, or deleted pages (not sure why those are included in the dumps).
In that case, there won't be a "mime-type" line, and no content :

```
<START_OF_ZIM_ARTICLE>
url: /Foo/bar.html
title: Foo bar
<END_OF_ZIM_ARTICLE>
...
```

Note that this format is meant to be streaming friendly, and you should
treat STDIN as a stream. *Don't* read all of STDIN before processing it if
you don't want to have a bad day : you probably don't want to put the whole
uncompressed content of wikipedia into your RAM (trololo).

Here is how to stream STDIN in ruby :

```
while article = $stdin.gets("<END_OF_ZIM_ARTICLE>\n")
  # do something with article.
end
```

And in python :

```
current_article = ""
for line in sys.stdin:
    current_article += line
    if "<END_OF_ZIM_ARTICLE>" in line:
        # do something with current_article
        current_article = ""
```
