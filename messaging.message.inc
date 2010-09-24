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
  // Unique message id
  public $mid = 0;
  // Sending method key: 'mail', 'sms', etc...
  public $method;
  // Destination address
  public $address;
  // Destination, to be decoded
  public $destination;
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
   * Get address to be sent to
   */
  function get_address() {
    return $this->address;
  }
  /**
   * Get rendered subject
   */
  function get_subject() {
    $this->render();
    return $this->subject;
  }
  /**
   * Get unrendered text
   */
  /**
   * Get rendered body
   */
  function get_body() {
    $this->render();
    return $this->body;
  }
  /**
   * Check parameters and go for alter hook
   */
  public function build() {
    if (empty($this->built)) {
      $this->set_destination();
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
      $this->rendered = time();
    }
  }
  /**
   * Send message through sending method.
   * 
   * The message may need to be processed and it can be queued or discarded yet.
   */
  public function send($destination = NULL) {
    if (isset($destination)) {
      $this->set_destination($destination);
    }
    if (!$this->processed) {
      return $this->process();
    }
    else {
      // The message has been processed and it is for sending
      $this->message_log('Sending message.');
      return $this->send_method()->message_send($this);
    }
  }

  /**
   * Check whether the message is to be sent / queued
   * 
   * @return boolean
   *   True if it needs further processing
   */
  protected function process() {
    // Preprocess: Build and prepare if not done before
    if (!$this->processed) {
      if (!$this->discard && !$this->prepared) {
        $this->prepare();
      }
    }
    // If everything is ok after preprocessing go for actual send/queue
    if ($this->prepared && !$this->discard && !$this->error) {
      // Mark as processed, so it is actually queued/sent
      $this->processed = TRUE;
      if ($this->test) {
        messaging_message_test($this);
        $this->success = TRUE;
      } 
      elseif ($this->queue) {
        $this->success = $this->queue();
        $this->module_invoke('queued');
      }
      else {
        // Send to one or more destinations
        if ($this->destination == self::MULTIPLE_DESTINATION) {
          $this->success = $this->send_multiple();
        }
        else {
          $this->success = $this->send();
        }
        if ($this->success) {
          $message->sent = time();
        }
        $this->send_method()->message_aftersend($this);
        $this->module_invoke('sent');
      }
      // Message done, maybe log and return
      $this->done();
      return $this->success;    
    }
    else {
      $this->discard('Failed processing.');
      messaging_debug('Discarded message during processing.', array('message' => $this));
      return FALSE;
    }
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
    if ($group = $this->send_method()->get_info('group')) {
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
}