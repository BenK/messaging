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
  // Messages status, nothing yet
  const STATUS_NONE = 0;
  // Message status, building
  const STATUS_BUILD = 1;
  // Message status, preparing
  const STATUS_PREPARE = 2;
  // Message status, processing
  const STATUS_PROCESS = 3;
  // Message status, sending
  const STATUS_DISPATCH = 4;
  // Message status, sending
  const STATUS_SEND = 5;
  // Message status: Already sent
  const STATUS_SENT = 10;
  // Message status: Queued
  const STATUS_QUEUE = 20;
  // Message status: Error
  const STATUS_ERROR = 30;
  // Errror code: Unespecified
  const ERROR = 1;
  // Error code: Destination error
  const ERROR_DESTINATION = 2;
  
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
  // User destination, if the message is intended for a single user
  public $uid;
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
  public $result = TRUE; // Result of last operation
  public $discard = FALSE;  // Marked to be discarded
  public $redirect = FALSE; // Redirect to a different sending method
  public $retry = FALSE;    // Retry sending
  public $deleted = FALSE;  // The message has been deleted, though it's still around for whatever use
  // Keep result of every step
  protected $process = array();
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
   * Get destinations for sending
   * @todo Return only the ones not sent
   */
  function get_destinations() {
    return $this->destination;
  }
  /**
   * Set user, if the message is intended for a single user try to find suitable method
   */
  function set_user($account) {
    $this->uid = $account->uid;
    if (empty($this->method)) {
      $this->method = Messaging_Method::default_method($account);
    }
    $this->add_user($account);   
    // Let the sending method prepare the message for this user
    $this->process_method('message_user');
    return $this;
  }
  /**
   * Get user, if the message is intended for a single user
   */
  function get_user() {
    return !empty($this->uid) ? user_load($this->uid) : NULL; 
  }
  /**
   * Add user as a destination
   * 
   * A real destination will be searched for user account
   */
  function add_user($account) {
    if ($destination = $this->send_method()->user_destination($account)) {
      $this->add_destination($destination);
      return TRUE;
    }
    else {
      return FALSE;
    }
  }
  
  /**
   * Check message status, will return FALSE if we should stop processing
   */
  function check_status() {
    return !$this->discard && !$this->error && !$this->deleted;
  }
  /**
   * Set success status and return status check
   */
  function set_status($status) {
    $this->status = $status;
    $this->result = TRUE;
    return $this->check_status();
  }
  /**
   * Check we have a sending method and a destination
   */
  function check_destination() {
    return !empty($this->method) && !empty($this->destination);
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
  protected function build() {
    if (!$this->process_todo('build')) {
      return TRUE;
    }
    else {
      $this->set_status(self::STATUS_BUILD);
      // Provides a hook for other modules to modify the message before sending
      drupal_alter('messaging_message', $this);
      // The message must be built, without errors and not for discarding
      return $this->process_done('build');
    }
  }

  /**
   * Prepare for sending through given method
   * 
   * At this stage, message can be redirected to a different method, marked for queueing, etc..
   */
  protected function prepare() {
    if (!$this->process_todo('prepare')) {
      return TRUE;
    }
    elseif ($this->process('build')) {
      $this->set_status(self::STATUS_PREPARE);
      // Prepare invoking one or more sending methods
      $this->process_method('message_prepare') ;
      // At this stage the message must be ready and have a valid destination
      return $this->process_done('prepare') && $this->check_destination();
    }
    else {
      return FALSE;
    }
  }
  
  /**
   * Render through sending method
   */
  public function render() {
    if (!$this->process_todo('render')) {
      return TRUE;
    }
    elseif ($this->process('prepare')) {
      $this->send_method()->message_render($this);
      return $this->process_done('render');
    }
    else {
      return FALSE;
    }
  }

  /**
   * Send message through sending method.
   * 
   * The message may need to be processed and it can be queued or discarded yet.
   */
  public function send() {
    if ($this->process_todo('dispatch')) {
      $this->send = 1;
      return $this->dispatch();
    }
    elseif ($this->check_destination()) {
      $this->set_status(self::STATUS_SEND);
      // The message has been processed and it is for sending
      $result = $this->send_method()->message_send($this);
      if ($result) {
        $this->sent = time();
      }
      return $this->set_status(self::STATUS_SENT, $result);  
    }
    else {
      $this->set_error(self::ERROR_DESTINATION, 'Method or destination invalid.');
      return FALSE;
    }
  }

  /**
   * Queue message using the messaging store.
   * 
   * This should happen only once per message, we check it hasn't been queued before.
   */
  public function queue() {
    if ($this->process_todo('dispatch')) {
      $this->queue = 1;
      return $this->dispatch();
    }
    elseif (!$this->process_todo('queue')) {
      return TRUE
    }
    else {
      $this->set_status(self::STATUS_QUEUE);
      $result = $this->send_method()->message_queue($this);
      return $this->process_done('queue', $result);
    }
  }
  
  /**
   * Check whether the message is to be sent / queued
   * 
   * @return boolean
   *   True if it needs further processing
   */
  protected function dispatch() {
    if ($this->process('prepare')) {
      // Mark as processed, so it is actually queued/sent
      $this->set_status(self::STATUS_DISPATCH);
      $this->process_done('dispatch');
      // Now, depending on message status, make a final decission.
      if (!empty($this->test)) {
        $this->result = TRUE;
        $this->module_invoke('test');
      } 
      elseif (!empty($this->queue)) {
        $this->result = $this->queue();
        $this->module_invoke('queued');
      }
      else {
        $this->result = $this->send();
        $this->module_invoke('sent');
      }
      // Message done, maybe log and return
      return $this->done();
    }
    else {
      $this->discard('Failed processing.');
      return FALSE;
    }
  }


  /**
   * Run a process step
   */
  protected function process($operation) {
    if ($this->process_todo($operation)) {
      $this->result = TRUE;
      return $this->$operation();
    }
    else {
      return TRUE;
    }
  }
  /**
   * Mark operation as done
   */
  protected function process_done($step, $success = TRUE) {
    $success = $success && $this->result && $this->check_status();
    $this->process[$step] = $success;
    return $success;
  }
  /**
   * Check whether an operation is still to do
   */
  protected function process_todo($step) {
    return empty($this->process[$step]);
  }
  /**
   * Process through sending method
   * 
   * This can go through multiple redirections, we stop it when we get to the same method again or process = FALSE
   */
  protected function process_method($operation) {
    if ($this->send_method()) {
      $methods = array();
      // The send method can stop this by setting 'discard' to TRUE
      while ($this->check_status() && !in_array($this->method, $methods)) {
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
  function set_error($code = 1, $text = 'ERROR') {
    // This will stop processing if we are in the middle of anything
    $this->status = self::STATUS_ERROR;
    $this->result = FALSE;
    $this->error = $code;
    $this->error_msg = $text;
    $this->message_log('Error processing message.');
    // By default, messages are set to be logged when errors happen
    if ($this->log_error) {
      $this->log = 1;
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
    $this->result = FALSE;
    $this->discard = TRUE;
    $this->debug('Message discarded during process', array('reason' => $reason));
    $this->delete();
  }

  /**
   * After the message has been processed
   */
  function done() {
    // Depending on parameters and what's happened so far we make the final log decision
    if ($this->log) {
      $this->log();
    }
    elseif ($this->discard) {
      $this->delete();
    }
    else {
      $this->delete();
    }
    return $this->result;
  }

  /**
   * Delete message from logs and store
   */
  public function delete() {
    if ($this->msid) {
      db_delete('messaging_message')
        ->condition('msid', $this->msid)
        ->execute();
      if (!empty($this->store) && ($store = messaging_store($this->store))) {
        $store->message_delete($this);
      }
    }
    $this->deleted = TRUE;
  }
  /**
   * Create or update message record
   */
  public function record($update = FALSE) {
    if (empty($this->msid) || $update) {
      $this->updated = time();
      if (!isset($this->created)) {
        $this->created = $this->updated;
      }
      return drupal_write_record('messaging_message', $this, $this->msid ? 'msid' : array());
    }
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
  /**
   * Build from db object
   */
  public static function build_object($template) {
    return new Messaging_Message($template);
  }
  /**
   * Invoke hook_messaging_message() on all modules
   */
  protected function module_invoke($op) {
    return module_invoke_all('messaging_message', $op, $this);
  }
  /**
   * Debug facility
   */
  protected function debug($text) {
    messaging_debug($text, array('message' => $this));
  }
}