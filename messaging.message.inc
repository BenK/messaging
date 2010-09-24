<?php
// $Id: messaging_message.class.inc,v 1.1.2.17 2010/06/21 12:05:28 jareyero Exp $
/**
 * @file
 * Drupal Messaging Framework - Message class file
 * 
 * The steps the message follows are
 * - build
 *   Parameters are checked, goes through alter hooks, etc...
 *   The sending method may change at this stage
 * - prepare
 *   Parameters are prepared using the sending method
 *   The sending method may still change at this stage 
 * - presend, send, aftersend
 *   If the message is to be sent right away
 * - queue, afterqueue
 *   If the message is to be queued
 * 
 * The message will be rendered right before sending or before being stored
 */

/**
 * Message class
 * 
 * This is a message with all the information, ready for sending
 */
class Messaging_Message {
  const STATUS_NONE = 0;
  const STATUS_SENT = 1;
  const STATUS_QUEUE = 2;
  const STATUS_ERROR = 3;
  
  // Unique message id
  public $msid = 0;
  // Queue id
  public $mqid = 0;
  // Status code
  public $status = 0;
  // Sending method key: 'mail', 'sms', etc...
  public $method;
  // Destination, to be decoded
  public $destination = array();
  // Results for each destination
  public $results = array();
  // Rendered message parts
  public $subject;
  public $body;
  // Unrendered message parts
  public $text;
  // Files linked to this message, object array indexed by 'fid'
  public $files;
  // Processing flags. They are 1 if the message is:
  public $queue = 0; // To be queued
  public $send = 0;  // To be sent
  public $log = 0; // To be logged
  // Message priority. If > 0 it won't be queued.
  public $priority = 0;
  // Multiple timestamps for different events
  public $created = 0;
  public $sent = 0;
  public $queued = 0;
  public $updated = 0;
  // Error code, error text
  public $error = 0;
  public $error_msg;
  // Temporary processing variables. These are not stored
  public $processed = FALSE; // Preprocessed and ready for action (send, queue)
  public $process = TRUE;   // Keep on processing
  public $result = TRUE;
  public $discard = FALSE;  // Marked to be discarded
  public $redirect = FALSE; // Redirect to a different sending method
  public $retry = FALSE;    // Retry sending
  public $success = TRUE;   // Result of last operation
  public $deleted = FALSE;  // The message has been deleted, though it's still around for whatever use

  /**
   * Constructor, with predefined array of data
   */ 
  function __construct($data = array()) {
    foreach ($data as $key => $value) {
      $this->$key = $value;
    }
  }
  /**
   * Add destination object
   */
  function add_destination($destinations) {
    $destinations = is_array($destinations) ? $destinations : array($destinations);
    // This will take care of duplicated destinations
    foreach ($destinations as $dest) {
      $this->destination[$dest->index()] = $dest;
    }
  }

  /**
   * Set full array of destinations
   */
  /**
   * Mark as sent for this destination key or for all  
   * 
   * @param $results
   *   Array of results indexed per destination key
   */
  function add_results($results) {
    $this->results = array_merge($this->results, $results);
  }
  
  /**
   * Get rendered subject
   */
  function get_subject() {
    $this->render();
    return $this->subject;
  }
  /**
   * Get rendered body
   */
  function get_body() {
    $this->render();
    return $this->body;
  }
  /**
   * Get message files
   */
  function get_files() {
    return !empty($this->files) ? $this->files : array();
  }
  /**
   * Check parameters and go for alter hook
   */
  public function build() {
    if (empty($this->built)) {
      // If message priority > 0 this is not to be queued
      if ($this->priority > 0) {
        $this->queue = 0;
      }
      // Provides a hook for other modules to modify the message before sending
      drupal_alter('messaging_message', $this);
      $this->built = TRUE;
    }
    return $this->built && !$this->discard;
  }
  /**
   * Build from db object
   */
  public static function build_object($template) {
    return new Messaging_Message($template);
  }
  /**
   * Prepare for sending through given method
   * 
   * At this stage, message can be redirected to a different method
   */
  public function prepare() {
    if (empty($this->prepared)) {
      if ($this->build()) {
        $this->set_destination();
        // Prepare invoking one or more sending methods
        $this->prepared = $this->process_method('message_prepare');
        messaging_debug("Prepared for send method.");
      }
      else {
        // Message building failed, we don't do anything else
        $this->prepared = FALSE;
        $this->message_log("Failed message build.");
      }
    }
    return $this->prepared;
  }
  
  /**
   * Render through sending method
   */
  public function render() {
    if (empty($this->rendered)) {
      $this->send_method()->message_render($this);
      $this->rendered[$this->method] = TRUE;
    }
  }
  /**
   * Send message through sending method.
   * 
   * The message may need to be processed and it can be queued or discarded yet.
   */
  public function send() {
    if (!$this->processed) {
      $this->send = 1;
      return $this->process();
    }
    else {
      // The message has been processed and it is for sending
      $this->result = $this->send_method()->message_send($this);
      if ($this->result) {
        $this->sent = time();
        $this->status = self::STATUS_SENT;
      }
      return $this->result;  
    }   
  }

  /**
   * Queue message using the messaging store.
   * 
   * This should happen only once per message, we check it hasn't been queued before.
   */
  public function queue() {
    $this->queue = 1;
    if (!$this->processed) {
      return $this->process();
    }
    elseif (!$this->queued) {
      $this->queued = $this->send_method()->message_queue($this);
      $this->status = self::STATUS_QUEUE;
      return $this->result = (bool)$this->queued;
    }
  }
  
  /**
   * Check whether the message is to be sent / queued
   * 
   * @return boolean
   *   True if it needs further processing
   */
  protected function process() {
    // If everything is ok after preprocessing go for actual send/queue
    if ($this->prepare() && !$this->discard && !$this->error) {
      // Mark as processed, so it is actually queued/sent
      $this->processed = TRUE;
      if ($this->test) {
        $this->success = TRUE;
        $this->module_invoke('test');
      } 
      elseif ($this->queue) {
        $this->success = $this->queue();
        $this->module_invoke('queued');
      }
      else {
        $this->success = $this->send();
        $this->module_invoke('sent');
      }
      // Message done, maybe log and return
      $this->done();
      return $this->success;    
    }
    else {
      $this->success = FALSE;
      $this->discard('Failed processing.');
    }
    return $this->success;
  }
  /**
   * Process through sending method
   * 
   * This can go through multiple redirections, we stop it when we get to the same method again or process = FALSE
   */
  protected function process_method($operation) {
    if ($this->send_method()) {
      $this->process = TRUE;
      $methods = array();
      // The send method can stop this by setting 'process' to FALSE or 'discard' to TRUE
      while ($this->process && !$this->discard && !in_array($this->method, $methods)) {
        $methods[] = $this->method;
        $this->send_method()->$operation($this);
      }
      return TRUE;
    } else {
      return FALSE;
    }
  }  
  /**
   * Get generic parameters
   */
  function get_params($key = NULL) {
    if ($key) {
      return isset($this->params[$key]) ? $this->params[$key] : array();
    }
    else {
      return isset($this->params) ? $this->params : array();
    }
  }
  /**
   * Get sending method parameters
   */
  function get_method_params($method = NULL) {
    $method = $method ? $method : $this->method;
    // First get specific parameters for this sending method
    $params = $this->get_params($method);
    // Check for specific parameters for this method group
    if ($group = messaging_send_method($method)->group) {
      $params += $this->get_params($group);
    }
    return $params;
  }

  /**
   * Get send method object
   */
  public function send_method() {
    return !empty($this->method) ? messaging_send_method($this->method) : NULL;
  }
  /**
   * Set error condition and stop processing
   * 
   * @param $text
   *   Error message to be stored
   */
  function set_error($text = 'ERROR', $code = 1) {
    // This will stop processing if we are in the middle of anything
    $this->status = self::STATUS_ERROR;
    $this->process = FALSE;
    $this->success = FALSE;
    $this->error = $code;
    $this->error_msg = $text;
    $this->message_log('Error processing message.');
    // By default, messages are set to be logged when errors happen
    if ($this->log_error) {
      $this->log = 1;
      $this->updated = TRUE;
    }
    elseif (!$this->log) {
      // Not for logging, discard
      $this->discard = TRUE;
    }
  }
  /**
   * Discard message
   */
  function discard($reason = 'No reason.') {
    $this->success = FALSE;
    $this->message_log('Message discarded during process', array('reason' => $reason));
    $this->delete();
  }

  /**
   * After the message has been processed
   */
  function done() {
    // Depending on parameters and what's happened so far we make the final queue/log decision
    if ($this->discard) {
      $this->discard('Done processing');
    } if ($this->error || $this->log) {
      $this->log();
    }
  }
  /**
   * Set error condition and stop processing
   * 
   * @param $text
   *   Error message to be stored
   */
  function set_error($text = 'ERROR', $code = 1) {
    // This will stop processing if we are in the middle of anything
    $this->process = FALSE;
    $this->success = FALSE;
    $this->error = $code;
    $this->error_msg = $text;
    $this->message_log('Error processing message.');
    // By default, messages are set to be logged when errors happen
    if ($this->log_error) {
      $this->log = 1;
      $this->updated = TRUE;
    }
    elseif (!$this->log) {
      // Not for logging, discard
      $this->discard = TRUE;
    }
  }   
  /**
   * Delete message from logs and store
   */
  public function delete() {
    if ($this->msid) {
      db_delete('messaging_message')->condition('msid' => $this->msid);
      if (!empty($this->store) && ($store = messaging_store($this->store))) {
        $store->message_delete($this);
      }
    }
    $this->deleted = TRUE;
  }
  /**
   * Create or update message record
   */
  public function record() {
    $this->updated = time();
    if (!isset($this->created)) {
      $this->created = $this->updated;
    }
    return drupal_write_record('messaging_message', $this, $this->msid ? 'msid' : array());
  }
  /**
   * Save message to store $name
   */
  public function save($name = 'store') {
    $this->record();
    if ($store = messaging_store($name)) {
      return $store->save_message($this);
    }
  }
  /**
   * Log message
   */
  public function log() {
    $message->logged = TRUE;
    return $message->save('logs');
  }
  /**
   * Load message by id
   */
  public static function load($msid) {
    $message = entity_load('messaging_message', array($eid));
    return $message ? $message[$msid] : FALSE;
  }

  /**
   * Load multiple events
   */
  public static function load_multiple($msids = array(), $conditions = array()) {
    return entity_load('messaging_message', $msids, $conditions);
  }
}