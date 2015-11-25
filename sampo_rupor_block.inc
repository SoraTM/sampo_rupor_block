<?php

define('RUPOR_CACHE_DIR', file_directory_temp());
define('RUPOR_MAX_LINKS', 5);
define('RUPOR_CACHE_TTL', 1);
define('RUPOR_INFORMER_VERSION', 2);
define('RUPOR_URL_FEED', 'http://rupor.sampo.ru/chronicles/tracker/feed');

class Rupor {

  var $_charset;

  function rupor($charset = 'UTF-8') {
    // charset is used as iconv parameter
    $this->_charset = $charset;
  }

  function getCache($id) {
    if (!file_exists(RUPOR_CACHE_DIR . $id)) {
      return FALSE;
    }
    $data = file_get_contents(RUPOR_CACHE_DIR . $id);
    if (empty($data)) {
      return FALSE;
    }
    $data = unserialize($data);
    if (time() > $data['time'] + $data['ttl']) {
      unlink(RUPOR_CACHE_DIR . $id);
      return FALSE;
    }
    return $data['data'];
  }

  function setCache($id, $data, $ttl = 300) {
    $contents = array(
        'time' => time(),
        'ttl' => $ttl,
        'data' => $data
    );

    if (file_put_contents(RUPOR_CACHE_DIR . $id, serialize($contents), LOCK_EX)) {
      return TRUE;
    }
    return FALSE;
  }

  function httpRequest($url) {
    if (FALSE && function_exists('curl_init')) {
      return $this->httpRequestCurl($url);
    } else {
      $result = $this->httpRequestSocket($url);
      if (isset($result->data)) {
        $result = $result->data;
      } else {
        $result = '';
      }
    }
    return $result;
  }

  function httpRequestSocket($url, $headers = array(), $method = 'GET', $data = NULL, $retry = 3) {
    $result = new stdClass();

    // Parse the URL and make sure we can handle the schema.
    $uri = parse_url($url);

    if ($uri == FALSE) {
      $result->error = 'unable to parse URL';
      return $result;
    }

    if (!isset($uri['scheme'])) {
      $result->error = 'missing schema';
      return $result;
    }

    switch ($uri['scheme']) {
      case 'http':
        $port = isset($uri['port']) ? $uri['port'] : 80;
        $host = $uri['host'] . ($port != 80 ? ':' . $port : '');
        $fp = @fsockopen($uri['host'], $port, $errno, $errstr, 1);
        break;
      case 'https':
        // Note: Only works for PHP 4.3 compiled with OpenSSL.
        $port = isset($uri['port']) ? $uri['port'] : 443;
        $host = $uri['host'] . ($port != 443 ? ':' . $port : '');
        $fp = @fsockopen('ssl://' . $uri['host'], $port, $errno, $errstr, 2);
        break;
      default:
        $result->error = 'invalid schema ' . $uri['scheme'];
        return $result;
    }

    // Make sure the socket opened properly.
    if (!$fp) {
      // When a network error occurs, we use a negative number so it does not
      // clash with the HTTP status codes.
      $result->code = -$errno;
      $result->error = trim($errstr);
      return $result;
    }

    // Construct the path to act on.
    $path = isset($uri['path']) ? $uri['path'] : '/';
    if (isset($uri['query'])) {
      $path .= '?' . $uri['query'];
    }

    // Create HTTP request.
    $defaults = array(
        // RFC 2616: "non-standard ports MUST, default ports MAY be included".
        // We don't add the port to prevent from breaking rewrite rules checking the
        // host that do not take into account the port number.
        'Host' => "Host: $host",
        'User-Agent' => 'User-Agent: rupor-parser-informer-' . RUPOR_INFORMER_VERSION,
        'Content-Length' => 'Content-Length: ' . strlen($data)
    );

    // If the server url has a user then attempt to use basic authentication
    if (isset($uri['user'])) {
      $defaults['Authorization'] = 'Authorization: Basic ' . base64_encode($uri['user'] . (!empty($uri['pass']) ? ":" . $uri['pass'] : ''));
    }

    foreach ($headers as $header => $value) {
      $defaults[$header] = $header . ': ' . $value;
    }

    $request = $method . ' ' . $path . " HTTP/1.0\r\n";
    $request .= implode("\r\n", $defaults);
    $request .= "\r\n\r\n";
    $request .= $data;

    $result->request = $request;

    fwrite($fp, $request);
    stream_set_timeout($fp, 1);

    // Fetch response.
    $response = '';
    while (!feof($fp) && $chunk = fread($fp, 2048)) {
      $response .= $chunk;
    }
    fclose($fp);

    // Parse response.
    list($split, $result->data) = explode("\r\n\r\n", $response, 2);
    $split = preg_split("/\r\n|\n|\r/", $split);

    list($protocol, $code, $text) = explode(' ', trim(array_shift($split)), 3);
    $result->headers = array();

    // Parse headers.
    while ($line = trim(array_shift($split))) {
      list($header, $value) = explode(':', $line, 2);
      if (isset($result->headers[$header]) && $header == 'Set-Cookie') {
        // RFC 2109: the Set-Cookie response header comprises the token Set-
        // Cookie:, followed by a comma-separated list of one or more cookies.
        $result->headers[$header] .= ',' . trim($value);
      } else {
        $result->headers[$header] = trim($value);
      }
    }

    $responses = array(
        100 => 'Continue', 101 => 'Switching Protocols',
        200 => 'OK', 201 => 'Created', 202 => 'Accepted', 203 => 'Non-Authoritative Information', 204 => 'No Content', 205 => 'Reset Content', 206 => 'Partial Content',
        300 => 'Multiple Choices', 301 => 'Moved Permanently', 302 => 'Found', 303 => 'See Other', 304 => 'Not Modified', 305 => 'Use Proxy', 307 => 'Temporary Redirect',
        400 => 'Bad Request', 401 => 'Unauthorized', 402 => 'Payment Required', 403 => 'Forbidden', 404 => 'Not Found', 405 => 'Method Not Allowed', 406 => 'Not Acceptable', 407 => 'Proxy Authentication Required', 408 => 'Request Time-out', 409 => 'Conflict', 410 => 'Gone', 411 => 'Length Required', 412 => 'Precondition Failed', 413 => 'Request Entity Too Large', 414 => 'Request-URI Too Large', 415 => 'Unsupported Media Type', 416 => 'Requested range not satisfiable', 417 => 'Expectation Failed',
        500 => 'Internal Server Error', 501 => 'Not Implemented', 502 => 'Bad Gateway', 503 => 'Service Unavailable', 504 => 'Gateway Time-out', 505 => 'HTTP Version not supported'
    );
    // RFC 2616 states that all unknown HTTP codes must be treated the same as the
    // base code in their class.
    if (!isset($responses[$code])) {
      $code = floor($code / 100) * 100;
    }

    switch ($code) {
      case 200: // OK
      case 304: // Not modified
        break;
      case 301: // Moved permanently
      case 302: // Moved temporarily
      case 307: // Moved temporarily
        $location = $result->headers['Location'];

        if ($retry) {
          $result = $this->httpRequestSocket($result->headers['Location'], $headers, $method, $data, --$retry);
          $result->redirect_code = $result->code;
        }
        $result->redirect_url = $location;

        break;
      default:
        $result->error = $text;
    }

    $result->code = $code;
    return $result;
  }

  function httpRequestCurl($url) {
    $tuCurl = curl_init();
    curl_setopt($tuCurl, CURLOPT_URL, $url);
    curl_setopt($tuCurl, CURLOPT_RETURNTRANSFER, 1);
    curl_setopt($tuCurl, CURLOPT_USERAGENT, 'rupor-parser-informer-' . RUPOR_INFORMER_VERSION);
    curl_setopt($tuCurl, CURLOPT_REFERER, @$_SERVER['HTTP_HOST']);
    curl_setopt($tuCurl, CURLOPT_FOLLOWLOCATION, 1);
    curl_setopt($tuCurl, CURLOPT_MAXREDIRS, 20);
    curl_setopt($tuCurl, CURLOPT_CONNECTTIMEOUT, 1);
    curl_setopt($tuCurl, CURLOPT_TIMEOUT, 2);
    $result = curl_exec($tuCurl);
    curl_close($tuCurl);
    return $result;
  }

  function getFeed() {
    $content = $this->getCache('rupor-rss');
    if ($content === FALSE) {
      $url = RUPOR_URL_FEED;
      $content = $this->httpRequest($url);
      if ($content) {
        $this->setCache('rupor-rss', $content, round(RUPOR_CACHE_TTL));
      } else {
        // some error
        $this->setCache('rupor-rss', '', round(RUPOR_CACHE_TTL / 2));
      }
    }

    $ruporBlock = '';
    if ($content) {
      $rss = simplexml_load_string($content);
      $index = 0;
      foreach ($rss->channel->item as $item) {
        $groupUrl = $item->group->attributes();
        $groupTitle = trim($item->group);
        $title = trim($item->title);
        $url = $item->link;
        if (empty($groupUrl))
          $groupUrl = 'http://rupor.sampo.ru/';
        if (empty($groupTitle))
          $groupTitle = 'Блог';
        if (empty($title))
          $title = 'Запись';
        if (empty($url))
          $url = 'http://rupor.sampo.ru/';


        $ruporBlock .= '<div class="rupor_line"><a href="' . $groupUrl . '" class="rupor_group_link">' . $this->htmlspecialchars_decode($groupTitle) . '</a><span class="rupor_arrow"> &rarr; </span><a href="' . $url . '" class="rupor_post_link">' . $this->htmlspecialchars_decode($title) . "</a></div>\n";
        if ($index >= RUPOR_MAX_LINKS - 1)
          break;
        $index++;
      }
    }
    return $ruporBlock;
  }

  function htmlspecialchars_decode($input) {
    // decode everything except &lt; and &gt;
    return str_replace(array('&quot;', '&amp;'), array('"', '&'), $input);
  }

}
